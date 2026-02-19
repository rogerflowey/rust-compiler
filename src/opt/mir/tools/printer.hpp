#pragma once

#include "opt/mir/ir/basic_block.hpp"
#include "opt/mir/ir/module.hpp"
#include "opt/mir/ir/node_id.hpp"
#include "opt/mir/ir/nodes.hpp"
#include "opt/mir/ir/slot.hpp"

#include <ostream>
#include <sstream>
#include <string>
#include <variant>

namespace opt::mir {

// Overloaded visitor helper
template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

/// Printer — text dump of an OptFunction for debugging.
class Printer {
public:
  explicit Printer(std::ostream &os) : os_(os) {}

  void print(const OptFunction &func) {
    os_ << "function @" << func.name << " {\n";
    print_slots(func);
    print_arena(func);
    print_blocks(func);
    os_ << "}\n";
  }

  void print(const OptModule &mod) {
    for (const auto &func : mod.functions) {
      print(func);
      os_ << "\n";
    }
  }

  /// Convenience: dump to string.
  static std::string to_string(const OptFunction &func) {
    std::ostringstream oss;
    Printer p(oss);
    p.print(func);
    return oss.str();
  }

private:
  std::ostream &os_;

  void print_slots(const OptFunction &func) {
    if (func.slots.empty())
      return;
    os_ << "  slots:\n";
    for (std::size_t i = 0; i < func.slots.size(); ++i) {
      const auto &s = func.slots[i];
      os_ << "    @" << i << ": ";
      switch (s.kind) {
      case Slot::Kind::StackLocal:
        os_ << "stack_local";
        break;
      case Slot::Kind::Parameter:
        os_ << "parameter";
        break;
      case Slot::Kind::HeapObject:
        os_ << "heap_object";
        break;
      case Slot::Kind::Global:
        os_ << "global";
        break;
      case Slot::Kind::MutRefParam:
        os_ << "mut_ref_param";
        break;
      }
      os_ << " type:" << raw_type(s.type);
      if (s.mutability == Mutability::Mutable) {
        os_ << " mutable";
      }
      if (!s.debug_name.empty())
        os_ << " \"" << s.debug_name << "\"";
      os_ << "\n";
    }
    os_ << "\n";
  }

  void print_arena(const OptFunction &func) {
    if (func.nodes.empty())
      return;
    os_ << "  arena:\n";
    for (std::size_t i = 0; i < func.nodes.size(); ++i) {
      os_ << "    %" << i << " = ";
      print_node_kind(func.nodes[i].kind);
      os_ << " : type:" << raw_type(func.nodes[i].type) << "\n";
    }
    os_ << "\n";
  }

  void print_node_kind(const NodeKind &kind) {
    std::visit(
        Overloaded{
            [&](const ConstantNode &n) {
              os_ << "Constant(";
              switch (n.value.kind) {
              case ConstantValue::Kind::Bool:
                os_ << (n.value.bits ? "true" : "false");
                break;
              case ConstantValue::Kind::Int:
                if (n.value.is_signed && (n.value.bits >> 63)) {
                  os_ << "-" << (~n.value.bits + 1);
                } else {
                  os_ << n.value.bits;
                }
                break;
              case ConstantValue::Kind::Char:
                os_ << "'" << static_cast<char>(n.value.bits) << "'";
                break;
              }
              os_ << ")";
            },
            [&](const BinaryOpNode &n) {
              os_ << binary_op_name(n.kind) << "(%" << raw(n.lhs) << ", %"
                  << raw(n.rhs) << ")";
            },
            [&](const UnaryOpNode &n) {
              os_ << unary_op_name(n.kind) << "(%" << raw(n.operand) << ")";
            },
            [&](const LoadNode &n) {
              os_ << "Load(%t" << raw(n.token) << ", ";
              print_place(n.place);
              os_ << ")";
            },
            [&](const CastNode &n) {
              os_ << "Cast(%" << raw(n.operand)
                  << " -> type:" << raw_type(n.target_type) << ")";
            },
            [&](const AddressOfNode &n) {
              os_ << "AddrOf(";
              print_place(n.place);
              os_ << ", "
                  << (n.mutability == Mutability::Mutable ? "mut" : "const")
                  << ")";
            },
            [&](const CallResultNode &n) {
              os_ << "CallResult(%t" << raw(n.call_token) << ")";
            }},
        kind);
  }

  void print_blocks(const OptFunction &func) {
    for (std::size_t i = 0; i < func.blocks.size(); ++i) {
      const auto &bb = func.blocks[i];
      os_ << "  bb" << i;
      if (bb.id == func.entry_block)
        os_ << " [entry]";
      os_ << ":";

      if (!bb.predecessors.empty()) {
        os_ << "  ; preds:";
        for (auto p : bb.predecessors)
          os_ << " bb" << raw(p);
      }
      os_ << "\n";

      for (auto iid : bb.inst_ids) {
        const auto &inst = func.get_inst(iid);
        os_ << "    ";
        print_pinned(inst.kind);
        os_ << "\n";
      }
    }
  }

  void print_pinned(const PinnedInstKind &kind) {
    std::visit(
        Overloaded{
            [&](const StoreInst &s) {
              os_ << "%t" << raw(s.t_out) << " = Store(%t" << raw(s.t_in)
                  << ", ";
              print_place(s.place);
              os_ << ", %" << raw(s.value) << ")";
            },
            [&](const BranchInst &b) {
              os_ << "(%t" << raw(b.t_true) << ", %t" << raw(b.t_false)
                  << ") = Branch(%t" << raw(b.t_in) << ", %" << raw(b.cond)
                  << ") -> [bb" << raw(b.bb_true) << ", bb" << raw(b.bb_false)
                  << "]";
            },
            [&](const JumpInst &j) {
              os_ << "Jump(%t" << raw(j.t_in) << ") -> bb" << raw(j.target);
            },
            [&](const TokenPhiInst &p) {
              os_ << "%t" << raw(p.t_out) << " = Phi(";
              for (std::size_t i = 0; i < p.incoming.size(); ++i) {
                if (i > 0)
                  os_ << ", ";
                os_ << "[%t" << raw(p.incoming[i].token) << ", bb"
                    << raw(p.incoming[i].block) << "]";
              }
              os_ << ")";
            },
            [&](const MemcopyInst &m) {
              os_ << "%t" << raw(m.t_out) << " = Memcopy(";
              print_place(m.dest);
              os_ << ", ";
              print_place(m.src);
              os_ << ", type:" << raw_type(m.type) << ")";
            },
            [&](const ReturnInst &r) {
              os_ << "Return(%t" << raw(r.t_in);
              if (r.value.has_value()) {
                os_ << ", %" << raw(*r.value);
              }
              os_ << ")";
            },
            [&](const CallInst &c) {
              os_ << "%t" << raw(c.t_out) << " = Call";
              if (c.target.kind == CallTarget::Kind::External) {
                os_ << " @" << c.target.name;
              } else {
                os_ << " #" << c.target.id;
              }
              os_ << "(%t" << raw(c.t_in);
              for (const auto &arg : c.args) {
                os_ << ", ";
                std::visit(Overloaded{[&](NodeId n) { os_ << "%" << raw(n); },
                                      [&](SlotId s) {
                                        os_ << "byval(@" << raw(s) << ")";
                                      }},
                           arg);
              }
              os_ << ")";
              if (c.sret_slot.has_value()) {
                os_ << " sret(@" << raw(*c.sret_slot) << ")";
              }
              if (c.result_type != type::invalid_type_id) {
                os_ << " -> type:" << raw_type(c.result_type);
              }
            }},
        kind);
  }

  static const char *binary_op_name(BinaryOpNode::Kind k) {
    switch (k) {
    case BinaryOpNode::Kind::IAdd:
      return "IAdd";
    case BinaryOpNode::Kind::UAdd:
      return "UAdd";
    case BinaryOpNode::Kind::ISub:
      return "ISub";
    case BinaryOpNode::Kind::USub:
      return "USub";
    case BinaryOpNode::Kind::IMul:
      return "IMul";
    case BinaryOpNode::Kind::UMul:
      return "UMul";
    case BinaryOpNode::Kind::IDiv:
      return "IDiv";
    case BinaryOpNode::Kind::UDiv:
      return "UDiv";
    case BinaryOpNode::Kind::IRem:
      return "IRem";
    case BinaryOpNode::Kind::URem:
      return "URem";
    case BinaryOpNode::Kind::BoolAnd:
      return "BoolAnd";
    case BinaryOpNode::Kind::BoolOr:
      return "BoolOr";
    case BinaryOpNode::Kind::BitAnd:
      return "BitAnd";
    case BinaryOpNode::Kind::BitXor:
      return "BitXor";
    case BinaryOpNode::Kind::BitOr:
      return "BitOr";
    case BinaryOpNode::Kind::Shl:
      return "Shl";
    case BinaryOpNode::Kind::ShrLogical:
      return "ShrLogical";
    case BinaryOpNode::Kind::ShrArithmetic:
      return "ShrArithmetic";
    case BinaryOpNode::Kind::ICmpEq:
      return "ICmpEq";
    case BinaryOpNode::Kind::ICmpNe:
      return "ICmpNe";
    case BinaryOpNode::Kind::ICmpLt:
      return "ICmpLt";
    case BinaryOpNode::Kind::ICmpLe:
      return "ICmpLe";
    case BinaryOpNode::Kind::ICmpGt:
      return "ICmpGt";
    case BinaryOpNode::Kind::ICmpGe:
      return "ICmpGe";
    case BinaryOpNode::Kind::UCmpEq:
      return "UCmpEq";
    case BinaryOpNode::Kind::UCmpNe:
      return "UCmpNe";
    case BinaryOpNode::Kind::UCmpLt:
      return "UCmpLt";
    case BinaryOpNode::Kind::UCmpLe:
      return "UCmpLe";
    case BinaryOpNode::Kind::UCmpGt:
      return "UCmpGt";
    case BinaryOpNode::Kind::UCmpGe:
      return "UCmpGe";
    case BinaryOpNode::Kind::BoolEq:
      return "BoolEq";
    case BinaryOpNode::Kind::BoolNe:
      return "BoolNe";
    }
    return "???";
  }

  static const char *unary_op_name(UnaryOpNode::Kind k) {
    switch (k) {
    case UnaryOpNode::Kind::Not:
      return "Not";
    case UnaryOpNode::Kind::Neg:
      return "Neg";
    }
    return "???";
  }

  void print_place(const Place &p) {
    std::visit(Overloaded{[&](SlotId s) { os_ << "@" << raw(s); },
                          [&](NodeId n) { os_ << "(%" << raw(n) << ")"; }},
               p.base);
    for (const auto &proj : p.projections) {
      std::visit(
          Overloaded{[&](const FieldProjection &f) { os_ << "." << f.index; },
                     [&](const IndexProjection &idx) {
                       os_ << "[%" << raw(idx.index) << "]";
                     }},
          proj);
    }
  }

  static std::uint32_t raw_type(type::TypeId t) {
    return static_cast<std::uint32_t>(t);
  }
};

} // namespace opt::mir
