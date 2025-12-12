Here is a concrete refactor plan to support LLVM parameter attributes, followed by the implementation steps.

### Refactor Plan

1.  **Data Model Update (`mir` namespace)**:
    *   Flesh out `mir::LlvmParamAttrs` to hold boolean flags for generic attributes (e.g., `noalias`, `nonnull`, `readonly`).
    *   *Note*: We will derive `sret` and `byval` attributes automatically from the `AbiParam` kind (since they require type arguments), while generic attributes will come from the struct.

2.  **Builder API Update (`llvmbuilder` namespace)**:
    *   Modify `llvmbuilder::FunctionParameter` to accept a list of attributes.
    *   Update `FunctionBuilder` to serialize these attributes into the IR string (e.g., `i32* noalias %p`).
    *   Update `emit_external_declaration` logic to support attributes in `declare` statements.

3.  **Emitter Logic Update (`codegen` namespace)**:
    *   Create a helper `get_param_attrs` that inspects both the `AbiParam::kind` (for `sret`/`byval` types) and `AbiParam::attrs` (for generic flags).
    *   Update `emit_function` to populate the builder parameters with these attributes.
    *   Update `emit_external_declaration` to format attributes correctly.

---

### Step 1: Update Data Structures (`mir/mir.hpp`)

Modify the placeholder structs to hold actual data.

```cpp
// In mir/mir.hpp

// LLVM parameter attributes
struct LlvmParamAttrs {
    bool noalias = false;
    bool nonnull = false;
    bool readonly = false;
    bool noundef = false;
    // Note: sret and byval are handled via AbiParam::kind because they need Types
};

// LLVM return attributes
struct LlvmReturnAttrs {
    bool noalias = false;
    bool nonnull = false;
    bool noundef = false;
};
```

### Step 2: Update LLVM Builder (`mir/codegen/llvmbuilder/builder.hpp` & `.cpp`)

We need to allow parameters to carry attributes.

**Header (`mir/codegen/llvmbuilder/builder.hpp`):**

```cpp
namespace llvmbuilder {

struct FunctionParameter {
    std::string type;
    std::string name;
    std::vector<std::string> attributes; // NEW: Store raw attribute strings (e.g., "noalias", "sret(%T)")
};

// ... inside FunctionBuilder class ...
class FunctionBuilder {
    // ... existing members
public:
    FunctionBuilder(std::string name,
                    std::string return_type,
                    std::vector<FunctionParameter> params); // Signature remains, struct changed
    // ...
};

} // namespace llvmbuilder
```

**Implementation (`mir/codegen/llvmbuilder/builder.cpp`):**

Update `FunctionBuilder::str()` to emit the attributes.

```cpp
// In FunctionBuilder::str()

std::string FunctionBuilder::str() const {
    std::ostringstream oss;
    oss << "define " << return_type_ << " " << name_ << "(";
    for (std::size_t i = 0; i < params_.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << params_[i].type;
        
        // Emit attributes
        for (const auto& attr : params_[i].attributes) {
            oss << " " << attr;
        }

        oss << " " << params_[i].name;
    }
    oss << ") {\n";
    // ... rest of the function
    return oss.str();
}
```

### Step 3: Update Emitter Logic (`mir/codegen/emitter.cpp`)

Now we wire the MIR information to the Builder.

**1. Add a helper to `Emitter` class (in header or anonymously in cpp) to resolve attributes:**

```cpp
// Helper to generate the attribute list for a specific AbiParam
std::vector<std::string> Emitter::resolve_param_attributes(
    const mir::AbiParam& abi_param, 
    const mir::MirFunctionSig& sig) {
    
    std::vector<std::string> attrs;

    // 1. Apply Generic Attributes from LlvmParamAttrs
    if (abi_param.attrs.noalias) attrs.push_back("noalias");
    if (abi_param.attrs.nonnull) attrs.push_back("nonnull");
    if (abi_param.attrs.readonly) attrs.push_back("readonly");
    if (abi_param.attrs.noundef) attrs.push_back("noundef");

    // 2. Apply ABI-Kind specific attributes (sret, byval)
    std::visit(mir::Overloaded{
        [&](const mir::AbiParamDirect&) {
            // Direct params usually don't imply extra ABI attributes
        },
        [&](const mir::AbiParamByValCallerCopy& k) {
            // "byval" requires a type in opaque pointer LLVM
            if (abi_param.param_index) {
                const auto& sem_param = sig.params[*abi_param.param_index];
                std::string type_name = module_.get_type_name(sem_param.type);
                attrs.push_back("byval(" + type_name + ")");
                
                // byval implies noalias and valid alignment usually
                // We add noalias explicitly as good practice for C++-like semantics
                if (std::find(attrs.begin(), attrs.end(), "noalias") == attrs.end()) {
                    attrs.push_back("noalias");
                }
                // We could add "nocapture" here too if strictly caller-owned copy
                attrs.push_back("nocapture");
            }
        },
        [&](const mir::AbiParamSRet& k) {
            // "sret" requires a type in opaque pointer LLVM
            const auto& sret_desc = std::get<mir::ReturnDesc::RetIndirectSRet>(sig.return_desc.kind);
            std::string type_name = module_.get_type_name(sret_desc.type);
            attrs.push_back("sret(" + type_name + ")");
            
            // sret pointers are always noalias and nocapture
            if (std::find(attrs.begin(), attrs.end(), "noalias") == attrs.end()) {
                attrs.push_back("noalias");
            }
            attrs.push_back("nocapture");
        }
    }, abi_param.kind);

    return attrs;
}
```

**2. Update `emit_function`:**

```cpp
void Emitter::emit_function(const mir::MirFunction &function) {
  current_function_ = &function;
  block_builders_.clear();

  std::vector<llvmbuilder::FunctionParameter> params;
  const mir::MirFunctionSig& sig = function.sig;

  for (const auto& abi_param : sig.abi_params) {
    std::string param_type_name = get_abi_param_type(abi_param, sig);
    std::string param_name;

    std::visit(mir::Overloaded{
      [&](const mir::AbiParamDirect&) {
        if (abi_param.param_index) {
          const auto& sem_param = sig.params[*abi_param.param_index];
          param_name = "%" + std::string("param_") + sem_param.debug_name;
        }
      },
      [&](const mir::AbiParamByValCallerCopy&) {
        if (abi_param.param_index) {
          const auto& sem_param = sig.params[*abi_param.param_index];
          param_name = "%" + std::string("param_") + sem_param.debug_name;
        }
      },
      [&](const mir::AbiParamSRet&) {
        param_name = "%sret.ptr";
      }
    }, abi_param.kind);

    if (!param_type_name.empty()) {
        // CHANGED: Resolve and pass attributes
        std::vector<std::string> attrs = resolve_param_attributes(abi_param, sig);
        params.push_back(llvmbuilder::FunctionParameter{
            std::move(param_type_name), 
            std::move(param_name),
            std::move(attrs)
        });
    }
  }

  // ... (Rest of function remains the same: return type calculation, builder creation, etc.)
  
  // ...
}
```

**3. Update `emit_external_declaration`:**

Since external declarations are manually formatted strings in the current implementation, we need to inject the attributes into the string generation.

```cpp
void Emitter::emit_external_declaration(const mir::ExternalFunction &function) {
  const mir::MirFunctionSig& sig = function.sig;
  
  std::vector<std::string> param_defs;
  
  for (const auto& abi_param : sig.abi_params) {
    std::string param_type = get_abi_param_type(abi_param, sig);
    if (param_type.empty()) continue;

    // Resolve attributes
    std::vector<std::string> attrs = resolve_param_attributes(abi_param, sig);
    
    std::ostringstream p_ss;
    p_ss << param_type;
    for(const auto& attr : attrs) {
        p_ss << " " << attr;
    }
    param_defs.push_back(p_ss.str());
  }
  
  // ... Return type calculation (same as before) ...
  std::string ret_type = std::visit(mir::Overloaded{
    [](const mir::ReturnDesc::RetNever&) { return std::string("void"); },
    [](const mir::ReturnDesc::RetVoid&) { return std::string("void"); },
    [this](const mir::ReturnDesc::RetDirect& k) {
      return module_.get_type_name(k.type);
    },
    [](const mir::ReturnDesc::RetIndirectSRet&) {
      return std::string("void"); 
    }
  }, sig.return_desc.kind);
  
  std::string params_str;
  for (std::size_t i = 0; i < param_defs.size(); ++i) {
    if (i > 0) {
      params_str += ", ";
    }
    params_str += param_defs[i];
  }
  
  std::string declaration = "declare dso_local " + ret_type + " @" + 
                            function.name + "(" + params_str + ")";
  module_.add_global(declaration);
}
```

### Summary of Resulting Behavior

1.  **Direct Parameters**:
    *   If `LlvmParamAttrs::noalias` is set -> `i32* noalias %p`.
2.  **SRet Parameters**:
    *   Automatically generates `ptr sret(%MyStruct) noalias nocapture %sret.ptr`.
    *   The `%MyStruct` type name is resolved via the `Emitter`'s module instance.
3.  **ByVal Parameters**:
    *   Automatically generates `ptr byval(%MyStruct) noalias nocapture %param`.

This implementation fully leverages the `llvmbuilder` architecture while keeping the semantic decisions (which attributes to apply) within the `Emitter` based on the high-level `AbiParam` definitions.