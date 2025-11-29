## High-level design goals

1. **Separate type system from HIR**
    * move `type` out of `semantic` namespace.
    * No `hir::StructDef*` / `hir::EnumDef*` inside `type::Type`.
    * The `type` layer owns the *canonical* description of structs/enums (namespace `type::`).

2. **Make MIR + LLVM IR work off `TypeId`**

    * MIR uses only `type::TypeId`.
    * Backend maps each `type::TypeId` to `llvm::Type*` (and struct declarations) via type tables + layout info.

---

## Phase 0 – Prereqs & naming

Add a tiny alias in `namespace mir` to decouple naming:

```cpp
namespace mir {
     using TypeId = type::TypeId;
     inline constexpr TypeId invalid_type_id = type::invalid_type_id;
}
```

No behavior change, but it semantically marks MIR’s dependency on the type system.

---

## Phase 1 – Introduce IDs and type tables (without using them yet)

### 1.1. Add IDs

In `type/type.hpp`:

```cpp
namespace type {

using StructId = std::uint32_t;
using EnumId   = std::uint32_t;
// Later: TypeCtorId, TyParamId, etc.
}
```

### 1.2. Change `StructType` / `EnumType` to use IDs

Right now:

```cpp
struct StructType {
     const hir::StructDef* symbol;
};
struct EnumType {
     const hir::EnumDef* symbol;
};
```

Change to:

```cpp
struct StructType {
     StructId id;
     bool operator==(const StructType& other) const { return id == other.id; }
};

struct EnumType {
     EnumId id;
     bool operator==(const EnumType& other) const { return id == other.id; }
};
```

Update `TypeHash` accordingly:

```cpp
size_t operator()(const StructType& st) const {
     return std::hash<StructId>()(st.id);
}
size_t operator()(const EnumType& et) const {
     return std::hash<EnumId>()(et.id);
}
```

**Temporary hack**: you’ll still map from HIR defs to these IDs via a side table.

### 1.3. Add basic struct/enum tables to `TypeContext`

Extend `TypeContext`:

```cpp
struct StructFieldInfo {
     std::string name;   // or ast::Identifier if you prefer
     type::TypeId type;  // resolved type::TypeId
     // optional: span::Span span;
};

struct StructInfo {
     std::string name;
     std::vector<StructFieldInfo> fields;
};

struct EnumVariantInfo {
     std::string name;
     // optional: span::Span span;
};

struct EnumInfo {
     std::string name;
     std::vector<EnumVariantInfo> variants;
};

class TypeContext {
public:
     // existing:
     type::TypeId get_id(const type::Type& t);

     // new:
     type::StructId register_struct(StructInfo info) {
          type::StructId id = static_cast<type::StructId>(structs.size());
          structs.push_back(std::move(info));
          return id;
     }

     type::EnumId register_enum(EnumInfo info) {
          type::EnumId id = static_cast<type::EnumId>(enums.size());
          enums.push_back(std::move(info));
          return id;
     }

     const StructInfo& get_struct(type::StructId id) const { return structs[id]; }
     const EnumInfo&   get_enum(type::EnumId id)   const { return enums[id]; }

private:
     std::unordered_map<type::Type, std::unique_ptr<type::Type>, TypeHash> registered_types;
     std::vector<StructInfo> structs;
     std::vector<EnumInfo>   enums;
};
```

At this phase, you don’t actually *use* these yet; they just exist.

---

## Phase 2 – Hook HIR structs/enums into the type tables

### 2.1. Add HIR ↔ type ID mappings

Create an environment used by your semantic pass (or add to an existing one):

```cpp
struct HirTypeEnv {
     std::unordered_map<const hir::StructDef*, type::StructId> struct_ids;
     std::unordered_map<const hir::EnumDef*,   type::EnumId>   enum_ids;
};
```

You can stash this in a broader `SemanticContext` if you have one.

### 2.2. During semantic analysis, populate `StructInfo` / `EnumInfo`

In your type-check / name-resolution phase, when you process each `hir::StructDef`:

1. Resolve each field’s type annotation to `type::TypeId`.

2. Build a `type::StructInfo`:

    ```cpp
    type::StructInfo info;
    info.name = struct_def.name.to_string();

    for (auto& field : struct_def.fields) {
         type::StructFieldInfo f;
         f.name = field.name.to_string();
         f.type = /* resolved type::TypeId from field_type_annotations[i] */;
         info.fields.push_back(std::move(f));
    }

    auto& type_ctx = type::TypeContext::get_instance();
    type::StructId sid = type_ctx.register_struct(std::move(info));
    env.struct_ids[&struct_def] = sid;
    ```

3. For enums, similar:

    ```cpp
    type::EnumInfo einfo;
    einfo.name = enum_def.name.to_string();
    for (auto& v : enum_def.variants) {
         type::EnumVariantInfo vi;
         vi.name = v.name.to_string();
         einfo.variants.push_back(std::move(vi));
    }
    type::EnumId eid = type_ctx.register_enum(std::move(einfo));
    env.enum_ids[&enum_def] = eid;
    ```

### 2.3. Construct `type::Type` for struct/enum

When you need a type like “the struct `S`”:

```cpp
type::StructType st{ env.struct_ids[&struct_def] };
type::Type ty{ st };
type::TypeId id = get_typeID(ty);
```

Same for enums.

At this point, **types no longer contain HIR pointers** — they depend only on IDs and the type tables.

You *may* still use `hir::StructDef::fields` in places, but the canonical truth is now in `TypeContext`.

---

## Phase 3 – Make HIR reference type IDs (optional but nice)

Right now `StructDef` looks like:

```cpp
struct StructDef {
     ast::Identifier name;
     std::vector<semantic::Field> fields;
     std::vector<TypeAnnotation> field_type_annotations;
     span::Span span;
     // ...
};
```

You can optionally add:

```cpp
struct StructDef {
     ast::Identifier name;
     // syntactic:
     std::vector<semantic::Field> fields;
     std::vector<TypeAnnotation> field_type_annotations;

     // semantic link:
     std::optional<type::StructId> struct_id;

     span::Span span;
};
```

And set `struct_id` when you register `StructInfo`.

Same for `EnumDef` with `type::EnumId`.

This makes it easy for any phase that only has `StructDef*` to hop into the type tables.

---

## Phase 4 – Extend type system for generics (design, then incremental implementation)

### 4.1. Introduce type params and type constructors

In `type`:

```cpp
using TyParamId  = std::uint32_t;
using TypeCtorId = std::uint32_t;

struct TypeParam {
     std::string name;
     // constraints/bounds later
};

struct TypeCtorInfo {
     std::string name;
     std::vector<TyParamId> params;
     // maybe link to a "definition" (struct/enum/alias/etc.)
};
```

### 4.2. Extend `Type` representation

Add variants:

```cpp
struct TypeParamRef {
     TyParamId id;
};

struct AppliedTypeCtor {
     TypeCtorId ctor;
     std::vector<type::TypeId> args;  // concrete type arguments
};

// or apply this only to generics; structs/enums can be modeled via ctors.
```

So your `TypeVariant` becomes something like:

```cpp
using TypeVariant = std::variant<
     PrimitiveKind,
     StructType,      // may become "applied ctor" + StructId internally
     EnumType,
     ReferenceType,
     ArrayType,
     UnitType,
     NeverType,
     UnderscoreType,
     TypeParamRef,
     AppliedTypeCtor   // optional pattern
>;
```

You don’t have to implement all generic logic now; just lay out the data structures.

### 4.3. HIR generic declarations

In `hir::StructDef`, you’ll add something like:

```cpp
struct StructDef {
     ast::Identifier name;
     std::vector<ast::Identifier> generic_params; // <T, U, ...>
     // ...
};
```

And the semantic pass will:

* Create `TyParamId`s for those.
* Register a `TypeCtorInfo` for `Foo<T>` (the constructor).
* Later, instantiations yield `type::TypeId`s via `TypeCtorId` + args.

You can defer full generics semantics (substitution, monomorphization) to a later milestone.

---

## Phase 5 – MIR: ensure it uses only concrete `TypeId`s

You already have MIR types as `type::TypeId` for temps, locals, params, etc.

The key invariant you eventually want:

> By the time a `mir::MirFunction` is constructed, all `type::TypeId`s it references are monomorphic (no type params left).

Implementation-wise:

1. Add assertions in MIR builder that reject `TypeParamRef` / generic constructors in `TypeId`s.
2. When you implement monomorphization:

    * Given a poly function `fn<T>(...)`, and a call `foo::<i32>(...)`, you:

      * Substitute `T -> i32` in all types.
      * Produce a new, monomorphic `hir::Function` or a monomorphic MIR template.
      * Build MIR from that with only concrete `type::TypeId`s.

You don’t have to fully implement this now for the structural refactor, but it’s important as a design goal.

---

## Phase 6 – LLVM IR backend: map TypeId → llvm::Type*

### 6.1. Add an LLVM type environment

In your LLVM backend:

```cpp
struct LlvmTypeEnv {
     llvm::LLVMContext& ctx;
     std::unordered_map<type::TypeId, llvm::Type*> cache;
};
```

### 6.2. Implement a recursive mapper

Core function:

```cpp
llvm::Type* getLLVMType(LlvmTypeEnv& env, type::TypeId ty_id) {
     if (auto it = env.cache.find(ty_id); it != env.cache.end())
          return it->second;

     const type::Type& ty = get_type_from_id(ty_id); // your helper
     llvm::Type* result = std::visit([&](auto&& t) -> llvm::Type* {
          using T = std::decay_t<decltype(t)>;
          if constexpr (std::is_same_v<T, PrimitiveKind>) {
                // map I32 → llvm::Type::getInt32Ty(env.ctx), etc.
          } else if constexpr (std::is_same_v<T, StructType>) {
                auto& type_ctx = type::TypeContext::get_instance();
                const auto& info = type_ctx.get_struct(t.id);
                std::vector<llvm::Type*> fields;
                fields.reserve(info.fields.size());
                for (auto& f : info.fields) {
                     fields.push_back(getLLVMType(env, f.type));
                }

                // Named struct:
                std::string name = "struct." + info.name; // or mangle with generics
                auto* llvmStruct = llvm::StructType::create(env.ctx, name);
                llvmStruct->setBody(fields);
                return llvmStruct;
          }
          // handle enums, references, arrays, etc.
     }, ty.value);

     env.cache.emplace(ty_id, result);
     return result;
}
```

This is where moving struct/enum info into the `type` tables really pays off: no HIR needed, no AST inspection.

---

## Phase 7 – Cleanup & hardening

Once everything compiles and passes basic tests:

1. **Remove remaining HIR pointer uses from `type::Type`**
    (should already be gone if you followed Phase 2).

2. **Audit all uses of `hir::StructDef::fields` / `EnumDef::variants`**:

    * For logic that should rely on canonical type truth, swap them to `TypeContext::get_struct / get_enum`.
    * Keep HIR’s syntactic containers only for:

      * Error reporting (“user wrote these fields”).
      * Early phases before type analysis runs.

3. **Enforce invariants with asserts**:

    * No `UnderscoreType` in MIR.
    * No `TypeParamRef` in MIR.
    * All `StructType`/`EnumType` IDs used in MIR have corresponding entries in the `type` tables.

4. **Add some end-to-end tests**:

    * Simple struct, no generics: field ordering & LLVM IR match expectation.
    * Simple enum, no payloads.

---

## Final intended design (one sentence)

> HIR owns syntax and declarations, `type` owns the canonical, ID-based type system (including structs, enums, and generics in the future), MIR carries only concrete `type::TypeId`s, and LLVM IR structs are declared by a backend that maps `type::TypeId` → `llvm::Type*` using struct/enum tables.
