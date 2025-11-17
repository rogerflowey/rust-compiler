#pragma once

#include "semantic/type/type.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mir {

// Strong ID wrappers keep SSA state readable and type-safe.
struct TempId {
	static constexpr uint32_t invalid_value = std::numeric_limits<uint32_t>::max();

	uint32_t value = invalid_value;

	constexpr bool is_valid() const { return value != invalid_value; }
	constexpr size_t index() const { return static_cast<size_t>(value); }

	friend constexpr bool operator==(TempId lhs, TempId rhs) { return lhs.value == rhs.value; }
	friend constexpr bool operator!=(TempId lhs, TempId rhs) { return !(lhs == rhs); }
};

struct LocalId {
	static constexpr uint32_t invalid_value = std::numeric_limits<uint32_t>::max();

	uint32_t value = invalid_value;

	constexpr bool is_valid() const { return value != invalid_value; }
	constexpr size_t index() const { return static_cast<size_t>(value); }

	friend constexpr bool operator==(LocalId lhs, LocalId rhs) { return lhs.value == rhs.value; }
	friend constexpr bool operator!=(LocalId lhs, LocalId rhs) { return !(lhs == rhs); }
};

struct BasicBlockId {
	static constexpr uint32_t invalid_value = std::numeric_limits<uint32_t>::max();

	uint32_t value = invalid_value;

	constexpr bool is_valid() const { return value != invalid_value; }
	constexpr size_t index() const { return static_cast<size_t>(value); }

	friend constexpr bool operator==(BasicBlockId lhs, BasicBlockId rhs) { return lhs.value == rhs.value; }
	friend constexpr bool operator!=(BasicBlockId lhs, BasicBlockId rhs) { return !(lhs == rhs); }
};

struct Constant {
	using Value = std::variant<int64_t, uint64_t, double, bool, char, std::string>;

	Value value{};
};

using Operand = std::variant<TempId, Constant>;

struct FieldProjection {
	using Selector = std::variant<std::string, size_t>; // named or tuple field
	Selector selector;
};

struct IndexProjection {
	TempId index;
};

struct DerefProjection {
	TempId pointer;
};

using Projection = std::variant<FieldProjection, IndexProjection, DerefProjection>;

struct Place {
	LocalId base;
	std::vector<Projection> projections;
};

struct PhiIncoming {
	BasicBlockId predecessor;
	TempId value;
};

struct PhiNode {
	TempId dest;
	std::vector<PhiIncoming> incoming;
};

struct BinaryOp {
	enum class Kind {
		Add,
		Sub,
		Mul,
		Div,
		Rem,
		BitAnd,
		BitOr,
		BitXor,
		Shl,
		Shr,
		Eq,
		Ne,
		Lt,
		Le,
		Gt,
		Ge,
		// LogicalAnd, //this should be desugared
		// LogicalOr
	};

	Kind kind;
	Operand lhs;
	Operand rhs;
};

struct UnaryOp {
	enum class Kind {
		Not,
		Neg,
		Deref,
		Ref,
		RefMut
	};

	Kind kind;
	Operand operand;
};

struct Ref {
	Place place;
};

struct Aggregate {
	enum class Kind { Struct, Array };

	Kind kind;
	std::vector<Operand> elements;
};

struct Cast {
	Operand value;
	semantic::TypeId target_type = nullptr;
};

using RValue = std::variant<BinaryOp, UnaryOp, Ref, Aggregate, Cast>;

struct Define {
	TempId dest;
	RValue value;
};

struct Load {
	TempId dest;
	Place src;
};

struct Assign {
	Place dest;
	Operand src;
};

struct Call {
	std::optional<TempId> dest;
	Operand func;
	std::vector<Operand> args;
};

using Statement = std::variant<Define, Load, Assign, Call>;

struct Goto {
	BasicBlockId target;
};

struct SwitchTarget {
	Constant value;
	BasicBlockId target;
};

struct SwitchInt {
	Operand discriminant;
	std::vector<SwitchTarget> targets;
	BasicBlockId otherwise_block;
};

struct ReturnTerm {
	std::optional<Operand> value;
};

struct Unreachable {};

using Terminator = std::variant<Goto, SwitchInt, ReturnTerm, Unreachable>;

struct BasicBlock {
	std::vector<PhiNode> phis;
	std::vector<Statement> statements;
	Terminator terminator;
};

struct LocalInfo {
	semantic::TypeId type = nullptr;
	std::string debug_name;
};

struct Function {
	std::string name;
	std::vector<semantic::TypeId> temp_types;
	std::vector<LocalInfo> locals;
	std::vector<BasicBlock> basic_blocks;
	BasicBlockId start_block;
};

struct Program {
	std::vector<Function> functions;
};

} // namespace mir

