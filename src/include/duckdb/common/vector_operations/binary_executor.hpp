//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/vector_operations/binary_executor.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include <functional>

namespace duckdb {

struct DefaultNullCheckOperator {
	template <class LEFT_TYPE, class RIGHT_TYPE> static inline bool Operation(LEFT_TYPE left, RIGHT_TYPE right) {
		return false;
	}
};

struct BinaryStandardOperatorWrapper {
	template <class FUNC, class OP, class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE>
	static inline RESULT_TYPE Operation(FUNC fun, LEFT_TYPE left, RIGHT_TYPE right, nullmask_t &nullmask, idx_t idx) {
		return OP::template Operation<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE>(left, right);
	}
};

struct BinarySingleArgumentOperatorWrapper {
	template <class FUNC, class OP, class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE>
	static inline RESULT_TYPE Operation(FUNC fun, LEFT_TYPE left, RIGHT_TYPE right, nullmask_t &nullmask, idx_t idx) {
		return OP::template Operation<LEFT_TYPE>(left, right);
	}
};

struct BinaryLambdaWrapper {
	template <class FUNC, class OP, class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE>
	static inline RESULT_TYPE Operation(FUNC fun, LEFT_TYPE left, RIGHT_TYPE right, nullmask_t &nullmask, idx_t idx) {
		return fun(left, right);
	}
};

struct BinaryExecutor {
private:
	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OPWRAPPER, class OP, class FUNC,
	          bool IGNORE_NULL, bool LEFT_CONSTANT, bool RIGHT_CONSTANT>
	static void ExecuteFlatLoop(LEFT_TYPE *__restrict ldata, RIGHT_TYPE *__restrict rdata,
	                        RESULT_TYPE *__restrict result_data, idx_t count,
	                        nullmask_t &nullmask, FUNC fun) {
		if (!LEFT_CONSTANT) {
			ASSERT_RESTRICT(ldata, ldata + count, result_data, result_data + count);
		}
		if (!RIGHT_CONSTANT) {
			ASSERT_RESTRICT(rdata, rdata + count, result_data, result_data + count);
		}
		if (IGNORE_NULL && nullmask.any()) {
			for(idx_t i = 0; i < count; i++) {
				if (!nullmask[i]) {
					auto lentry = ldata[LEFT_CONSTANT ? 0 : i];
					auto rentry = rdata[RIGHT_CONSTANT ? 0 : i];
					result_data[i] = OPWRAPPER::template Operation<FUNC, OP, LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE>(
					    fun, lentry, rentry, nullmask, i);
				}
			}
		} else {
			for(idx_t i = 0; i < count; i++) {
				auto lentry = ldata[LEFT_CONSTANT ? 0 : i];
				auto rentry = rdata[RIGHT_CONSTANT ? 0 : i];
				result_data[i] = OPWRAPPER::template Operation<FUNC, OP, LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE>(
				    fun, lentry, rentry, nullmask, i);
			}
		}
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OPWRAPPER, class OP, class FUNC, bool IGNORE_NULL>
	static void ExecuteConstant(Vector &left, Vector &right, Vector &result, FUNC fun) {
		auto ldata = (LEFT_TYPE *)left.GetData();
		auto rdata = (RIGHT_TYPE *)right.GetData();
		auto result_data = (RESULT_TYPE *)result.GetData();

		result.vector_type = VectorType::CONSTANT_VECTOR;
		if (left.nullmask[0] || right.nullmask[0]) {
			result.nullmask[0] = true;
			return;
		}
		result_data[0] = OPWRAPPER::template Operation<FUNC, OP, LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE>(
			fun, ldata[0], rdata[0], result.nullmask, 0);
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OPWRAPPER, class OP, class FUNC,
	          bool IGNORE_NULL, bool LEFT_CONSTANT, bool RIGHT_CONSTANT>
	static void ExecuteFlat(Vector &left, Vector &right, Vector &result, FUNC fun) {
		auto ldata = (LEFT_TYPE *)left.GetData();
		auto rdata = (RIGHT_TYPE *)right.GetData();
		auto result_data = (RESULT_TYPE *)result.GetData();

		if ((LEFT_CONSTANT && left.nullmask[0]) || (RIGHT_CONSTANT && right.nullmask[0])) {
			// either left or right is constant NULL: result is constant NULL
			result.vector_type = VectorType::CONSTANT_VECTOR;
			result.nullmask[0] = true;
			return;
		}

		result.vector_type = VectorType::FLAT_VECTOR;
		if (LEFT_CONSTANT) {
			result.nullmask = right.nullmask;
		} else if (RIGHT_CONSTANT) {
			result.nullmask = left.nullmask;
		} else {
			result.nullmask = left.nullmask | right.nullmask;
		}
		ExecuteFlatLoop<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, FUNC, IGNORE_NULL, LEFT_CONSTANT, RIGHT_CONSTANT>(ldata, rdata, result_data, result.size(), result.nullmask, fun);
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OPWRAPPER, class OP, class FUNC, bool IGNORE_NULL>
	static void ExecuteGenericLoop(LEFT_TYPE *__restrict ldata, RIGHT_TYPE *__restrict rdata,
	                        RESULT_TYPE *__restrict result_data, SelectionVector *__restrict lsel, SelectionVector *__restrict rsel, idx_t count,
	                        nullmask_t &lnullmask, nullmask_t &rnullmask, nullmask_t &result_nullmask, FUNC fun) {
		if (lnullmask.any() || rnullmask.any()) {
			for(idx_t i = 0; i < count; i++) {
				auto lindex = lsel->get_index(i);
				auto rindex = rsel->get_index(i);
				if (!lnullmask[lindex] && !rnullmask[rindex]) {
					auto lentry = ldata[lindex];
					auto rentry = rdata[rindex];
					result_data[i] = OPWRAPPER::template Operation<FUNC, OP, LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE>(
					    fun, lentry, rentry, result_nullmask, i);
				} else {
					result_nullmask[i] = true;
				}
			}
		} else {
			for(idx_t i = 0; i < count; i++) {
				auto lentry = ldata[lsel->get_index(i)];
				auto rentry = rdata[rsel->get_index(i)];
				result_data[i] = OPWRAPPER::template Operation<FUNC, OP, LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE>(
				    fun, lentry, rentry, result_nullmask, i);
			}
		}
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OPWRAPPER, class OP, class FUNC,
	          bool IGNORE_NULL>
	static void ExecuteGeneric(Vector &left, Vector &right, Vector &result, FUNC fun) {
		data_ptr_t ldata, rdata;
		SelectionVector *lsel, *rsel;

		left.Orrify(&lsel, &ldata);
		right.Orrify(&rsel, &rdata);

		result.vector_type = VectorType::FLAT_VECTOR;
		ExecuteGenericLoop<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, FUNC, IGNORE_NULL>((LEFT_TYPE*) ldata, (RIGHT_TYPE*) rdata, (RESULT_TYPE*) result.GetData(), lsel, rsel, result.size(), left.nullmask, right.nullmask, result.nullmask, fun);
	}



	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OPWRAPPER, class OP, class FUNC, bool IGNORE_NULL>
	static void ExecuteSwitch(Vector &left, Vector &right, Vector &result, FUNC fun) {
		assert(left.SameCardinality(right) && left.SameCardinality(result));
		if (left.vector_type == VectorType::CONSTANT_VECTOR && right.vector_type == VectorType::CONSTANT_VECTOR) {
			ExecuteConstant<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, FUNC, IGNORE_NULL>(left, right, result, fun);
		} else if (left.vector_type == VectorType::FLAT_VECTOR && right.vector_type == VectorType::CONSTANT_VECTOR) {
			ExecuteFlat<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, FUNC, IGNORE_NULL, false, true>(left, right, result, fun);
		} else if (left.vector_type == VectorType::CONSTANT_VECTOR && right.vector_type == VectorType::FLAT_VECTOR) {
			ExecuteFlat<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, FUNC, IGNORE_NULL, true, false>(left, right, result, fun);
		} else if (left.vector_type == VectorType::FLAT_VECTOR && right.vector_type == VectorType::FLAT_VECTOR) {
			ExecuteFlat<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, FUNC, IGNORE_NULL, false, false>(left, right, result, fun);
		} else {
			ExecuteGeneric<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, FUNC, IGNORE_NULL>(left, right, result, fun);
		}
	}

public:
	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, bool IGNORE_NULL = false,
	          class FUNC = std::function<RESULT_TYPE(LEFT_TYPE, RIGHT_TYPE)>>
	static void Execute(Vector &left, Vector &right, Vector &result, FUNC fun) {
		ExecuteSwitch<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, BinaryLambdaWrapper, bool, FUNC, IGNORE_NULL>(left, right,
		                                                                                                result, fun);
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OP, bool IGNORE_NULL = false,
	          class OPWRAPPER = BinarySingleArgumentOperatorWrapper>
	static void Execute(Vector &left, Vector &right, Vector &result) {
		ExecuteSwitch<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, OPWRAPPER, OP, bool, IGNORE_NULL>(left, right, result, false);
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class RESULT_TYPE, class OP, bool IGNORE_NULL = false>
	static void ExecuteStandard(Vector &left, Vector &right, Vector &result) {
		ExecuteSwitch<LEFT_TYPE, RIGHT_TYPE, RESULT_TYPE, BinaryStandardOperatorWrapper, OP, bool, IGNORE_NULL>(
		    left, right, result, false);
	}

public:
	template <class LEFT_TYPE, class RIGHT_TYPE, class OP>
	static idx_t SelectConstant(Vector &left, Vector &right, SelectionVector &result) {
		auto ldata = (LEFT_TYPE *)left.GetData();
		auto rdata = (RIGHT_TYPE *)right.GetData();

		// both sides are constant, return either 0 or the count
		// in this case we do not fill in the result selection vector at all
		if (left.nullmask[0] || right.nullmask[0] || !OP::Operation(ldata[0], rdata[0])) {
			return 0;
		} else {
			return left.size();
		}
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class OP, bool LEFT_CONSTANT, bool RIGHT_CONSTANT>
	static inline idx_t SelectFlatLoop(LEFT_TYPE *__restrict ldata, RIGHT_TYPE *__restrict rdata, SelectionVector &result,
	                               idx_t count, nullmask_t &nullmask) {
		idx_t result_count = 0;
		if (nullmask.any()) {
			for(idx_t i = 0; i < count; i++) {
				if (!nullmask[i]) {
					auto lentry = ldata[LEFT_CONSTANT ? 0 : i];
					auto rentry = rdata[RIGHT_CONSTANT ? 0 : i];
					if (OP::Operation(lentry, rentry)) {
						result.set_index(result_count++, i);
					}
				}
			}
		} else {
			for(idx_t i = 0; i < count; i++) {
				auto lentry = ldata[LEFT_CONSTANT ? 0 : i];
				auto rentry = rdata[RIGHT_CONSTANT ? 0 : i];
				if (OP::Operation(lentry, rentry)) {
					result.set_index(result_count++, i);
				}
			}
		}
		return result_count;
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class OP, bool LEFT_CONSTANT, bool RIGHT_CONSTANT>
	static idx_t SelectFlat(Vector &left, Vector &right, SelectionVector &result) {
		auto ldata = (LEFT_TYPE *)left.GetData();
		auto rdata = (RIGHT_TYPE *)right.GetData();

		if (LEFT_CONSTANT && left.nullmask[0]) {
			return 0;
		}
		if (RIGHT_CONSTANT && right.nullmask[0]) {
			return 0;
		}

		if (LEFT_CONSTANT) {
			return SelectFlatLoop<LEFT_TYPE, RIGHT_TYPE, OP, LEFT_CONSTANT, RIGHT_CONSTANT>(ldata, rdata, result, left.size(), right.nullmask);
		} else if (RIGHT_CONSTANT) {
			return SelectFlatLoop<LEFT_TYPE, RIGHT_TYPE, OP, LEFT_CONSTANT, RIGHT_CONSTANT>(ldata, rdata, result, left.size(), left.nullmask);
		} else {
			auto nullmask = left.nullmask | right.nullmask;
			return SelectFlatLoop<LEFT_TYPE, RIGHT_TYPE, OP, LEFT_CONSTANT, RIGHT_CONSTANT>(ldata, rdata, result, left.size(), nullmask);
		}
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class OP>
	static inline idx_t SelectGenericLoop(LEFT_TYPE *__restrict ldata, RIGHT_TYPE *__restrict rdata, SelectionVector *__restrict lsel, SelectionVector *__restrict rsel, idx_t count, nullmask_t &lnullmask, nullmask_t &rnullmask, SelectionVector &result) {
		idx_t result_count = 0;
		if (lnullmask.any() || rnullmask.any()) {
			for(idx_t i = 0; i < count; i++) {
				auto lindex = lsel->get_index(i);
				auto rindex = rsel->get_index(i);
				if (!lnullmask[lindex] && !rnullmask[rindex]) {
					auto lentry = ldata[lindex];
					auto rentry = rdata[rindex];
					if (OP::Operation(lentry, rentry)) {
						result.set_index(result_count++, i);
					}
				}
			}
		} else {
			for(idx_t i = 0; i < count; i++) {
				auto lentry = ldata[lsel->get_index(i)];
				auto rentry = rdata[rsel->get_index(i)];
				if (OP::Operation(lentry, rentry)) {
					result.set_index(result_count++, i);
				}
			}
		}
		return result_count;
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class OP>
	static idx_t SelectGeneric(Vector &left, Vector &right, SelectionVector &result) {
		data_ptr_t ldata, rdata;
		SelectionVector *lsel, *rsel;

		left.Orrify(&lsel, &ldata);
		right.Orrify(&rsel, &rdata);

		SelectGenericLoop<LEFT_TYPE, RIGHT_TYPE, OP>((LEFT_TYPE*) ldata, (RIGHT_TYPE*) rdata, lsel, rsel, left.size(), left.nullmask, right.nullmask, result);
	}

	template <class LEFT_TYPE, class RIGHT_TYPE, class OP>
	static idx_t Select(Vector &left, Vector &right, SelectionVector &result) {
		assert(left.SameCardinality(right));
		if (left.vector_type == VectorType::CONSTANT_VECTOR && right.vector_type == VectorType::CONSTANT_VECTOR) {
			return SelectConstant<LEFT_TYPE, RIGHT_TYPE, OP>(left, right, result);
		} else if (left.vector_type == VectorType::CONSTANT_VECTOR && right.vector_type == VectorType::FLAT_VECTOR) {
			return SelectFlat<LEFT_TYPE, RIGHT_TYPE, OP, true, false>(left, right, result);
		} else if (left.vector_type == VectorType::FLAT_VECTOR && right.vector_type == VectorType::CONSTANT_VECTOR) {
			return SelectFlat<LEFT_TYPE, RIGHT_TYPE, OP, false, true>(left, right, result);
		} else if (left.vector_type == VectorType::FLAT_VECTOR && right.vector_type == VectorType::FLAT_VECTOR) {
			return SelectFlat<LEFT_TYPE, RIGHT_TYPE, OP, false, false>(left, right, result);
		} else {
			return SelectGeneric<LEFT_TYPE, RIGHT_TYPE, OP>(left, right, result);
		}
	}
};

} // namespace duckdb
