/*
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 * Adriano dos Santos Fernandes - refactored from pass1.cpp, gen.cpp, cmp.cpp, par.cpp and evl.cpp
 */

#include "firebird.h"
#include "../common/common.h"
#include "../dsql/AggNodes.h"
#include "../dsql/ExprNodes.h"
#include "../dsql/node.h"
#include "../jrd/jrd.h"
#include "../jrd/blr.h"
#include "../jrd/btr.h"
#include "../jrd/exe.h"
#include "../jrd/tra.h"
#include "../jrd/recsrc/RecordSource.h"
#include "../jrd/blb_proto.h"
#include "../jrd/cmp_proto.h"
#include "../jrd/evl_proto.h"
#include "../jrd/intl_proto.h"
#include "../jrd/mov_proto.h"
#include "../jrd/par_proto.h"
#include "../dsql/ddl_proto.h"
#include "../dsql/errd_proto.h"
#include "../dsql/gen_proto.h"
#include "../dsql/make_proto.h"
#include "../dsql/pass1_proto.h"
#include "../dsql/utld_proto.h"
#include "../jrd/DataTypeUtil.h"

using namespace Firebird;
using namespace Jrd;

#include "gen/blrtable.h"

namespace Jrd {


AggNode::AggNode(MemoryPool& pool, const AggInfo& aAggInfo, bool aDistinct, bool aDialect1,
			dsql_nod* aArg)
	: TypedNode<ValueExprNode, ExprNode::TYPE_AGGREGATE>(pool),
	  aggInfo(aAggInfo),
	  distinct(aDistinct),
	  dialect1(aDialect1),
	  dsqlArg(aArg),
	  arg(NULL),
	  asb(NULL),
	  indexed(false)
{
	addChildNode(dsqlArg, arg);
}

AggNode* AggNode::dsqlPass(DsqlCompilerScratch* dsqlScratch)
{
	if (dsqlScratch->isPsql())
	{
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
				  Arg::Gds(isc_dsql_command_err));
	}

	if (!(dsqlScratch->inSelectList || dsqlScratch->inWhereClause || dsqlScratch->inGroupByClause ||
		  dsqlScratch->inHavingClause || dsqlScratch->inOrderByClause))
	{
		// not part of a select list, where clause, group by clause,
		// having clause, or order by clause
		ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
				  Arg::Gds(isc_dsql_agg_ref_err));
	}

	return dsqlCopy(dsqlScratch);
}

void AggNode::print(string& text, Array<dsql_nod*>& nodes) const
{
	text = string("AggNode (") + aggInfo.name + ")";

	for (dsql_nod* const* const* i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
	{
		if (**i)
			nodes.add(**i);
	}

	ExprNode::print(text, nodes);
}

bool AggNode::dsqlAggregateFinder(AggregateFinder& visitor)
{
	if (visitor.window || visitor.ignoreSubSelects)
		return false;

	bool aggregate = false;
	USHORT localDeepestLevel = 0;

	// If we are already in an aggregate function don't search inside
	// sub-selects and other aggregate-functions for the deepest field
	// used else we would have a wrong deepest_level value.

	{	// scope
		// We disable visiting of subqueries to handle this kind of query:
		//   select (select sum((select outer.column from inner1)) from inner2)
		//     from outer;
		AutoSetRestore<USHORT> autoDeepestLevel(&visitor.deepestLevel, 0);
		AutoSetRestore<bool> autoIgnoreSubSelects(&visitor.ignoreSubSelects, true);

		for (dsql_nod*** i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
			visitor.visit(*i);

		localDeepestLevel = visitor.deepestLevel;
	}

	if (localDeepestLevel == 0)
	{
		// ASF: There were no usage of a field of this scope [COUNT(*) or SUM(1)] or
		// they are inside a subquery [COUNT((select outer.field from inner))].

		// So the level found (deepestLevel) is the one of the current query in
		// processing.
		visitor.deepestLevel = visitor.currentLevel;
	}
	else
		visitor.deepestLevel = localDeepestLevel;

	// If the deepestLevel is the same as the current scopeLevel this is an
	// aggregate that belongs to the current context.
	if (visitor.deepestLevel == visitor.dsqlScratch->scopeLevel)
		aggregate = true;
	else
	{
		// Check also for a nested aggregate that could belong to this context. Example:
		//   select (select count(count(outer.n)) from inner) from outer

		AutoSetRestore<USHORT> autoDeepestLevel(&visitor.deepestLevel, localDeepestLevel);

		for (dsql_nod*** i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
			aggregate |= visitor.visit(*i);
	}

	return aggregate;
}

bool AggNode::dsqlAggregate2Finder(Aggregate2Finder& visitor)
{
	if (visitor.windowOnly)
		return false;

	bool found = false;
	FieldFinder fieldFinder(visitor.checkScopeLevel, visitor.matchType);

	for (dsql_nod*** i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
		found |= fieldFinder.visit(*i);

	if (!fieldFinder.getField())
	{
		// For example COUNT(*) is always same scope_level (node->nod_count = 0)
		// Normaly COUNT(*) is the only way to come here but something stupid
		// as SUM(5) is also possible.
		// If currentScopeLevelEqual is false scopeLevel is always higher
		switch (visitor.matchType)
		{
			case FIELD_MATCH_TYPE_LOWER_EQUAL:
			case FIELD_MATCH_TYPE_EQUAL:
				return visitor.currentScopeLevelEqual;

			///case FIELD_MATCH_TYPE_HIGHER_EQUAL:
			///	return true;

			case FIELD_MATCH_TYPE_LOWER:	// Not used here
			///case FIELD_MATCH_TYPE_HIGHER:
				fb_assert(false);
				return false;

			default:
				fb_assert(false);
		}
	}

	return found;
}

bool AggNode::dsqlInvalidReferenceFinder(InvalidReferenceFinder& visitor)
{
	bool invalid = false;

	if (!visitor.insideOwnMap)
	{
		// We are not in an aggregate from the same scope_level so
		// check for valid fields inside this aggregate
		invalid |= ExprNode::dsqlInvalidReferenceFinder(visitor);
	}

	if (!visitor.insideHigherMap)
	{
		for (dsql_nod*** i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
		{
			// If there's another aggregate with the same scope_level or
			// an higher one then it's a invalid aggregate, because
			// aggregate-functions from the same context can't
			// be part of each other.
			if (Aggregate2Finder::find(visitor.context->ctx_scope_level,
					FIELD_MATCH_TYPE_EQUAL, false, **i))
			{
				// Nested aggregate functions are not allowed
				ERRD_post(Arg::Gds(isc_sqlerr) << Arg::Num(-104) <<
						  Arg::Gds(isc_dsql_agg_nested_err));
			}
		}
	}

	return invalid;
}

bool AggNode::dsqlSubSelectFinder(SubSelectFinder& /*visitor*/)
{
	return false;
}

bool AggNode::dsqlFieldRemapper(FieldRemapper& visitor)
{
	AggregateFinder aggFinder(visitor.dsqlScratch, false);
	aggFinder.deepestLevel = visitor.dsqlScratch->scopeLevel;
	aggFinder.currentLevel = visitor.currentLevel;

	if (dsqlAggregateFinder(aggFinder))
	{
		if (!visitor.window && visitor.dsqlScratch->scopeLevel == aggFinder.deepestLevel)
		{
			visitor.replaceNode(PASS1_post_map(visitor.dsqlScratch, visitor.getCurrentNode(),
				visitor.context, visitor.partitionNode, visitor.orderNode));
			return false;
		}
	}

	for (dsql_nod*** i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
		visitor.visit(*i);

	return false;
}

bool AggNode::dsqlMatch(const ExprNode* other, bool ignoreMapCast) const
{
	if (!ExprNode::dsqlMatch(other, ignoreMapCast))
		return false;

	const AggNode* o = other->as<AggNode>();
	fb_assert(o);

	// ASF: We compare name address. That should be ok, as we have only one AggInfo instance
	// per function.
	return aggInfo.blr == o->aggInfo.blr && aggInfo.name == o->aggInfo.name &&
		distinct == o->distinct && dialect1 == o->dialect1;
}

void AggNode::setParameterName(dsql_par* parameter) const
{
	parameter->par_name = parameter->par_alias = aggInfo.name;
}

void AggNode::genBlr(DsqlCompilerScratch* dsqlScratch)
{
	if (aggInfo.blr)	// Is this a standard aggregate function?
		dsqlScratch->appendUChar((distinct ? aggInfo.distinctBlr : aggInfo.blr));
	else	// This is a new window function.
	{
		dsqlScratch->appendUChar(blr_agg_function);
		dsqlScratch->appendNullString(aggInfo.name);

		unsigned count = 0;
		for (dsql_nod*** i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
		{
			if (**i)
				++count;
		}

		dsqlScratch->appendUChar(UCHAR(count));
	}

	for (dsql_nod*** i = dsqlChildNodes.begin(); i != dsqlChildNodes.end(); ++i)
	{
		if (**i)
			GEN_expr(dsqlScratch, **i);
	}
}

ExprNode* AggNode::pass2(thread_db* tdbb, CompilerScratch* csb)
{
	ExprNode::pass2(tdbb, csb);

	dsc desc;
	CMP_get_desc(tdbb, csb, node, &desc);
	node->nod_impure = CMP_impure(csb, sizeof(impure_value_ex));

	return this;
}

void AggNode::aggInit(thread_db* /*tdbb*/, jrd_req* request) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);
	impure->vlux_count = 0;

	if (distinct)
	{
		// Initialize a sort to reject duplicate values.

		Database* database = request->req_attachment->att_database;
		impure_agg_sort* asbImpure = request->getImpure<impure_agg_sort>(asb->impure);

		// Get rid of the old sort areas if this request has been used already.
		delete asbImpure->iasb_sort;
		asbImpure->iasb_sort = NULL;

		asbImpure->iasb_sort = FB_NEW(request->req_sorts.getPool()) Sort(
			database, &request->req_sorts, ROUNDUP_LONG(asb->length),
			asb->keyItems.getCount(), 1, asb->keyItems.begin(),
			RecordSource::rejectDuplicate, 0);
	}
}

void AggNode::aggPass(thread_db* tdbb, jrd_req* request) const
{
	dsc* desc = NULL;

	if (arg)
	{
		desc = EVL_expr(tdbb, arg);
		if (request->req_flags & req_null)
			return;

		if (distinct)
		{
			fb_assert(asb);

			// "Put" the value to sort.
			impure_agg_sort* asbImpure = request->getImpure<impure_agg_sort>(asb->impure);
			UCHAR* data;
			asbImpure->iasb_sort->put(tdbb, reinterpret_cast<ULONG**>(&data));

			MOVE_CLEAR(data, asb->length);

			if (asb->intl)
			{
				// Convert to an international byte array.
				dsc to;
				to.dsc_dtype = dtype_text;
				to.dsc_flags = 0;
				to.dsc_sub_type = 0;
				to.dsc_scale = 0;
				to.dsc_ttype() = ttype_sort_key;
				to.dsc_length = asb->keyItems[0].skd_length;
				to.dsc_address = data;
				INTL_string_to_key(tdbb, INTL_TEXT_TO_INDEX(desc->getTextType()),
					desc, &to, INTL_KEY_UNIQUE);
			}

			dsc toDesc = asb->desc;
			toDesc.dsc_address = data +
				(asb->intl ? asb->keyItems[1].skd_offset : 0);
			MOV_move(tdbb, desc, &toDesc);

			return;
		}
	}

	aggPass(tdbb, request, desc);
}

void AggNode::aggFinish(thread_db* /*tdbb*/, jrd_req* request) const
{
	if (asb)
	{
		impure_agg_sort* const asbImpure = request->getImpure<impure_agg_sort>(asb->impure);
		delete asbImpure->iasb_sort;
		asbImpure->iasb_sort = NULL;
	}
}

dsc* AggNode::execute(thread_db* tdbb, jrd_req* request) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (impure->vlu_blob)
	{
		BLB_close(tdbb, impure->vlu_blob);
		impure->vlu_blob = NULL;
	}

	if (distinct)
	{
		impure_agg_sort* asbImpure = request->getImpure<impure_agg_sort>(asb->impure);
		dsc desc = asb->desc;

		// Sort the values already "put" to sort.
		asbImpure->iasb_sort->sort(tdbb);

		// Now get the sorted/projected values and compute the aggregate.

		while (true)
		{
			UCHAR* data;
			asbImpure->iasb_sort->get(tdbb, reinterpret_cast<ULONG**>(&data));

			if (!data)
			{
				// We are done, close the sort.
				delete asbImpure->iasb_sort;
				asbImpure->iasb_sort = NULL;
				break;
			}

			desc.dsc_address = data + (asb->intl ? asb->keyItems[1].skd_offset : 0);

			aggPass(tdbb, request, &desc);
		}
	}

	return aggExecute(tdbb, request);
}


//--------------------


static AggNode::Register<AvgAggNode> avgAggInfo("AVG", blr_agg_average, blr_agg_average_distinct);

AvgAggNode::AvgAggNode(MemoryPool& pool, bool aDistinct, bool aDialect1, dsql_nod* aArg)
	: AggNode(pool, avgAggInfo, aDistinct, aDialect1, aArg),
	  tempImpure(0)
{
	dsqlCompatDialectVerb = "avg";
}

DmlNode* AvgAggNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp)
{
	AvgAggNode* node = FB_NEW(pool) AvgAggNode(pool,
		(blrOp == blr_agg_average_distinct),
		(csb->csb_g_flags & csb_blr_version4));
	node->arg = PAR_parse_node(tdbb, csb, VALUE);
	return node;
}

void AvgAggNode::make(DsqlCompilerScratch* dsqlScratch, dsql_nod* /*thisNode*/, dsc* desc)
{
	MAKE_desc(dsqlScratch, desc, dsqlArg);
	desc->setNullable(true);

	if (dialect1)
	{
		if (!DTYPE_IS_NUMERIC(desc->dsc_dtype) && !DTYPE_IS_TEXT(desc->dsc_dtype))
		{
			ERRD_post(Arg::Gds(isc_expression_eval_err) <<
					  Arg::Gds(isc_dsql_agg_wrongarg) << Arg::Str("AVG"));
		}
		else if (DTYPE_IS_TEXT(desc->dsc_dtype))
		{
			desc->dsc_dtype = dtype_double;
			desc->dsc_length = sizeof(double);
		}
	}
	else
	{
		if (!DTYPE_IS_NUMERIC(desc->dsc_dtype))
		{
			ERRD_post(Arg::Gds(isc_expression_eval_err) <<
					  Arg::Gds(isc_dsql_agg2_wrongarg) << Arg::Str("AVG"));
		}
		else if (DTYPE_IS_EXACT(desc->dsc_dtype))
		{
			desc->dsc_dtype = dtype_int64;
			desc->dsc_length = sizeof(SINT64);
		}
		else
		{
			desc->dsc_dtype = dtype_double;
			desc->dsc_length = sizeof(double);
		}
	}
}

void AvgAggNode::getDesc(thread_db* tdbb, CompilerScratch* csb, dsc* desc)
{
	CMP_get_desc(tdbb, csb, arg, desc);

	if (dialect1)
	{
		if (!(DTYPE_IS_NUMERIC(desc->dsc_dtype) || DTYPE_IS_TEXT(desc->dsc_dtype)))
		{
			if (desc->dsc_dtype != dtype_unknown)
				ERR_post(Arg::Gds(isc_datype_notsup));	// data type not supported for arithmetic
		}

		desc->dsc_dtype = DEFAULT_DOUBLE;
		desc->dsc_length = sizeof(double);
		desc->dsc_scale = 0;
		desc->dsc_sub_type = 0;
		desc->dsc_flags = 0;
		return;
	}

	// In V6, the average of an exact type is computed in SINT64, rather than double as in prior
	// releases.
	switch (desc->dsc_dtype)
	{
		case dtype_short:
		case dtype_long:
		case dtype_int64:
			desc->dsc_dtype = dtype_int64;
			desc->dsc_length = sizeof(SINT64);
			desc->dsc_sub_type = 0;
			desc->dsc_flags = 0;
			node->nod_scale = desc->dsc_scale;
			break;

		case dtype_unknown:
			desc->dsc_dtype = dtype_unknown;
			desc->dsc_length = 0;
			desc->dsc_scale = 0;
			desc->dsc_sub_type = 0;
			desc->dsc_flags = 0;
			break;

		default:
			if (!DTYPE_IS_NUMERIC(desc->dsc_dtype))
			{
				if (desc->dsc_dtype == dtype_quad)
					IBERROR(224);	// msg 224 quad word arithmetic not supported

				ERR_post(Arg::Gds(isc_datype_notsup));	// data type not supported for arithmetic
			}

			desc->dsc_dtype = DEFAULT_DOUBLE;
			desc->dsc_length = sizeof(double);
			desc->dsc_scale = 0;
			desc->dsc_sub_type = 0;
			desc->dsc_flags = 0;
			node->nod_flags |= nod_double;
			break;
	}
}

ValueExprNode* AvgAggNode::copy(thread_db* tdbb, NodeCopier& copier)
{
	AvgAggNode* node = FB_NEW(*tdbb->getDefaultPool()) AvgAggNode(*tdbb->getDefaultPool(),
		distinct, dialect1);
	node->arg = copier.copy(tdbb, arg);
	return node;
}

ExprNode* AvgAggNode::pass2(thread_db* tdbb, CompilerScratch* csb)
{
	if (dialect1)
		node->nod_flags |= nod_double;

	// We need a second descriptor in the impure area for AVG.
	tempImpure = CMP_impure(csb, sizeof(impure_value_ex));

	return AggNode::pass2(tdbb, csb);
}

void AvgAggNode::aggInit(thread_db* tdbb, jrd_req* request) const
{
	AggNode::aggInit(tdbb, request);

	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (dialect1)
	{
		impure->vlu_desc.makeDouble(&impure->vlu_misc.vlu_double);
		impure->vlu_misc.vlu_double = 0;
	}
	else
	{
		// Initialize the result area as an int64. If the field being aggregated is approximate
		// numeric, the first call to add will convert the descriptor to double.
		impure->make_int64(0, node->nod_scale);
	}
}

void AvgAggNode::aggPass(thread_db* /*tdbb*/, jrd_req* request, dsc* desc) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);
	++impure->vlux_count;

	if (dialect1)
		ArithmeticNode::add(desc, impure, node, blr_add);
	else
		ArithmeticNode::add2(desc, impure, node, blr_add);
}

dsc* AvgAggNode::aggExecute(thread_db* tdbb, jrd_req* request) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (!impure->vlux_count)
		return NULL;

	dsc temp;
	SINT64 i;
	double d;

	if (!dialect1 && impure->vlu_desc.dsc_dtype == dtype_int64)
	{
		i = *((SINT64*) impure->vlu_desc.dsc_address) / impure->vlux_count;
		temp.makeInt64(impure->vlu_desc.dsc_scale, &i);
	}
	else
	{
		d = MOV_get_double(&impure->vlu_desc) / impure->vlux_count;
		temp.makeDouble(&d);
	}

	impure_value_ex* impureTemp = request->getImpure<impure_value_ex>(tempImpure);
	EVL_make_value(tdbb, &temp, impureTemp);

	return &impureTemp->vlu_desc;
}

AggNode* AvgAggNode::dsqlCopy(DsqlCompilerScratch* dsqlScratch) const
{
	return FB_NEW(getPool()) AvgAggNode(getPool(), distinct, dialect1,
		PASS1_node(dsqlScratch, dsqlArg));
}


//--------------------


static AggNode::Register<ListAggNode> listAggInfo("LIST", blr_agg_list, blr_agg_list_distinct);

ListAggNode::ListAggNode(MemoryPool& pool, bool aDistinct, dsql_nod* aArg, dsql_nod* aDelimiter)
	: AggNode(pool, listAggInfo, aDistinct, false, aArg),
	  dsqlDelimiter(aDelimiter),
	  delimiter(NULL)
{
	addChildNode(dsqlDelimiter, delimiter);
}

DmlNode* ListAggNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp)
{
	ListAggNode* node = FB_NEW(pool) ListAggNode(pool,
		(blrOp == blr_agg_list_distinct));
	node->arg = PAR_parse_node(tdbb, csb, VALUE);
	node->delimiter = PAR_parse_node(tdbb, csb, VALUE);
	return node;
}

void ListAggNode::make(DsqlCompilerScratch* dsqlScratch, dsql_nod* /*thisNode*/, dsc* desc)
{
	MAKE_desc(dsqlScratch, desc, dsqlArg);
	desc->makeBlob(desc->getBlobSubType(), desc->getTextType());
	desc->setNullable(true);
}

bool ListAggNode::setParameterType(DsqlCompilerScratch* dsqlScratch, dsql_nod* thisNode,
	dsql_nod* node, bool forceVarChar)
{
	return PASS1_set_parameter_type(dsqlScratch, dsqlArg, node, forceVarChar) |
		PASS1_set_parameter_type(dsqlScratch, dsqlDelimiter, node, forceVarChar);
}

void ListAggNode::getDesc(thread_db* tdbb, CompilerScratch* csb, dsc* desc)
{
	CMP_get_desc(tdbb, csb, arg, desc);
	desc->makeBlob(desc->getBlobSubType(), desc->getTextType());
}

ValueExprNode* ListAggNode::copy(thread_db* tdbb, NodeCopier& copier)
{
	ListAggNode* node = FB_NEW(*tdbb->getDefaultPool()) ListAggNode(*tdbb->getDefaultPool(),
		distinct);
	node->arg = copier.copy(tdbb, arg);
	node->delimiter = copier.copy(tdbb, delimiter);
	return node;
}

void ListAggNode::aggInit(thread_db* tdbb, jrd_req* request) const
{
	AggNode::aggInit(tdbb, request);

	// We don't know here what should be the sub-type and text-type.
	// Defer blob creation for when first record is found.
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);
	impure->vlu_blob = NULL;
	impure->vlu_desc.dsc_dtype = 0;
}

void ListAggNode::aggPass(thread_db* tdbb, jrd_req* request, dsc* desc) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (!impure->vlu_blob)
	{
		impure->vlu_blob = BLB_create(tdbb, request->req_transaction,
			&impure->vlu_misc.vlu_bid);
		impure->vlu_desc.makeBlob(desc->getBlobSubType(), desc->getTextType(),
			(ISC_QUAD*) &impure->vlu_misc.vlu_bid);
	}

	MoveBuffer buffer;
	UCHAR* temp;
	int len;

	if (impure->vlux_count)
	{
		const dsc* const delimiterDesc = EVL_expr(tdbb, delimiter);
		if (request->req_flags & req_null)
		{
			// Mark the result as NULL.
			impure->vlu_desc.dsc_dtype = 0;
			return;
		}

		len = MOV_make_string2(tdbb, delimiterDesc, impure->vlu_desc.getTextType(),
			&temp, buffer, false);
		BLB_put_data(tdbb, impure->vlu_blob, temp, len);
	}

	++impure->vlux_count;
	len = MOV_make_string2(tdbb, desc, impure->vlu_desc.getTextType(),
		&temp, buffer, false);
	BLB_put_data(tdbb, impure->vlu_blob, temp, len);
}

dsc* ListAggNode::aggExecute(thread_db* tdbb, jrd_req* request) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (distinct)
	{
		if (impure->vlu_blob)
		{
			BLB_close(tdbb, impure->vlu_blob);
			impure->vlu_blob = NULL;
		}
	}

	if (!impure->vlux_count || !impure->vlu_desc.dsc_dtype)
		return NULL;

	return &impure->vlu_desc;
}

AggNode* ListAggNode::dsqlCopy(DsqlCompilerScratch* dsqlScratch) const
{
	return FB_NEW(getPool()) ListAggNode(getPool(), distinct,
		PASS1_node(dsqlScratch, dsqlArg), PASS1_node(dsqlScratch, dsqlDelimiter));
}


//--------------------


static RegisterNode<CountAggNode> regCountAggNodeLegacy(blr_agg_count);

static AggNode::Register<CountAggNode> countAggInfo("COUNT", blr_agg_count2, blr_agg_count_distinct);

CountAggNode::CountAggNode(MemoryPool& pool, bool aDistinct, dsql_nod* aArg)
	: AggNode(pool, countAggInfo, aDistinct, false, aArg)
{
}

DmlNode* CountAggNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp)
{
	CountAggNode* node = FB_NEW(pool) CountAggNode(pool,
		(blrOp == blr_agg_count_distinct));
	if (blrOp != blr_agg_count)
		node->arg = PAR_parse_node(tdbb, csb, VALUE);
	return node;
}

void CountAggNode::make(DsqlCompilerScratch* /*dsqlScratch*/, dsql_nod* /*thisNode*/, dsc* desc)
{
	desc->makeLong(0);
}

void CountAggNode::genBlr(DsqlCompilerScratch* dsqlScratch)
{
	if (dsqlArg)
		AggNode::genBlr(dsqlScratch);
	else
		dsqlScratch->appendUChar(blr_agg_count);
}

void CountAggNode::getDesc(thread_db* /*tdbb*/, CompilerScratch* /*csb*/, dsc* desc)
{
	desc->dsc_dtype = dtype_long;
	desc->dsc_length = sizeof(SLONG);
	desc->dsc_scale = 0;
	desc->dsc_sub_type = 0;
	desc->dsc_flags = 0;
}

ValueExprNode* CountAggNode::copy(thread_db* tdbb, NodeCopier& copier)
{
	CountAggNode* node = FB_NEW(*tdbb->getDefaultPool()) CountAggNode(*tdbb->getDefaultPool(),
		distinct);
	node->arg = copier.copy(tdbb, arg);
	return node;
}

void CountAggNode::aggInit(thread_db* tdbb, jrd_req* request) const
{
	AggNode::aggInit(tdbb, request);

	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);
	impure->make_long(0);
}

void CountAggNode::aggPass(thread_db* /*tdbb*/, jrd_req* request, dsc* /*desc*/) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);
	++impure->vlu_misc.vlu_long;
}

dsc* CountAggNode::aggExecute(thread_db* /*tdbb*/, jrd_req* request) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (!impure->vlu_desc.dsc_dtype)
		return NULL;

	return &impure->vlu_desc;
}

AggNode* CountAggNode::dsqlCopy(DsqlCompilerScratch* dsqlScratch) const
{
	return FB_NEW(getPool()) CountAggNode(getPool(), distinct,
		PASS1_node(dsqlScratch, dsqlArg));
}


//--------------------


static AggNode::Register<SumAggNode> sumAggInfo("SUM", blr_agg_total, blr_agg_total_distinct);

SumAggNode::SumAggNode(MemoryPool& pool, bool aDistinct, bool aDialect1, dsql_nod* aArg)
	: AggNode(pool, sumAggInfo, aDistinct, aDialect1, aArg)
{
	dsqlCompatDialectVerb = "sum";
}

DmlNode* SumAggNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp)
{
	SumAggNode* node = FB_NEW(pool) SumAggNode(pool,
		(blrOp == blr_agg_total_distinct),
		(csb->csb_g_flags & csb_blr_version4));
	node->arg = PAR_parse_node(tdbb, csb, VALUE);
	return node;
}

void SumAggNode::make(DsqlCompilerScratch* dsqlScratch, dsql_nod* /*thisNode*/, dsc* desc)
{
	MAKE_desc(dsqlScratch, desc, dsqlArg);
	desc->setNullable(true);

	if (dialect1)
	{
		if (!DTYPE_IS_NUMERIC(desc->dsc_dtype) && !DTYPE_IS_TEXT(desc->dsc_dtype))
		{
			ERRD_post(Arg::Gds(isc_expression_eval_err) <<
					  Arg::Gds(isc_dsql_agg_wrongarg) << Arg::Str("SUM"));
		}
		else if (desc->dsc_dtype == dtype_short)
		{
			desc->dsc_dtype = dtype_long;
			desc->dsc_length = sizeof(SLONG);
		}
		else if (desc->dsc_dtype == dtype_int64)
		{
			desc->dsc_dtype = dtype_double;
			desc->dsc_length = sizeof(double);
		}
		else if (DTYPE_IS_TEXT(desc->dsc_dtype))
		{
			desc->dsc_dtype = dtype_double;
			desc->dsc_length = sizeof(double);
		}
	}
	else
	{
		if (!DTYPE_IS_NUMERIC(desc->dsc_dtype))
		{
			ERRD_post(Arg::Gds(isc_expression_eval_err) <<
					  Arg::Gds(isc_dsql_agg2_wrongarg) << Arg::Str("SUM"));
		}
		else if (DTYPE_IS_EXACT(desc->dsc_dtype))
		{
			desc->dsc_dtype = dtype_int64;
			desc->dsc_length = sizeof(SINT64);
		}
		else
		{
			desc->dsc_dtype = dtype_double;
			desc->dsc_length = sizeof(double);
		}
	}
}

void SumAggNode::getDesc(thread_db* tdbb, CompilerScratch* csb, dsc* desc)
{
	CMP_get_desc(tdbb, csb, arg, desc);

	if (dialect1)
	{
		switch (desc->dsc_dtype)
		{
			case dtype_short:
				desc->dsc_dtype = dtype_long;
				desc->dsc_length = sizeof(SLONG);
				node->nod_scale = desc->dsc_scale;
				desc->dsc_sub_type = 0;
				desc->dsc_flags = 0;
				return;

			case dtype_unknown:
				desc->dsc_dtype = dtype_unknown;
				desc->dsc_length = 0;
				node->nod_scale = 0;
				desc->dsc_sub_type = 0;
				desc->dsc_flags = 0;
				return;

			case dtype_long:
			case dtype_int64:
			case dtype_real:
			case dtype_double:
			case dtype_text:
			case dtype_cstring:
			case dtype_varying:
				desc->dsc_dtype = DEFAULT_DOUBLE;
				desc->dsc_length = sizeof(double);
				desc->dsc_scale = 0;
				desc->dsc_sub_type = 0;
				desc->dsc_flags = 0;
				node->nod_flags |= nod_double;
				return;

			case dtype_quad:
				desc->dsc_dtype = dtype_quad;
				desc->dsc_length = sizeof(SQUAD);
				desc->dsc_sub_type = 0;
				desc->dsc_flags = 0;
				node->nod_scale = desc->dsc_scale;
				node->nod_flags |= nod_quad;
#ifdef NATIVE_QUAD
				return;
#endif

			default:
				fb_assert(false);
				// fall into

			case dtype_sql_time:
			case dtype_sql_date:
			case dtype_timestamp:
			case dtype_blob:
			case dtype_array:
			case dtype_dbkey:
				break;	// break to error reporting code
		}
	}
	else
	{
		switch (desc->dsc_dtype)
		{
			case dtype_short:
			case dtype_long:
			case dtype_int64:
				desc->dsc_dtype = dtype_int64;
				desc->dsc_length = sizeof(SINT64);
				node->nod_scale = desc->dsc_scale;
				desc->dsc_flags = 0;
				return;

			case dtype_unknown:
				desc->dsc_dtype = dtype_unknown;
				desc->dsc_length = 0;
				node->nod_scale = 0;
				desc->dsc_sub_type = 0;
				desc->dsc_flags = 0;
				return;

			case dtype_real:
			case dtype_double:
			case dtype_text:
			case dtype_cstring:
			case dtype_varying:
				desc->dsc_dtype = DEFAULT_DOUBLE;
				desc->dsc_length = sizeof(double);
				desc->dsc_scale = 0;
				desc->dsc_sub_type = 0;
				desc->dsc_flags = 0;
				node->nod_flags |= nod_double;
				return;

			case dtype_quad:
				desc->dsc_dtype = dtype_quad;
				desc->dsc_length = sizeof(SQUAD);
				desc->dsc_sub_type = 0;
				desc->dsc_flags = 0;
				node->nod_scale = desc->dsc_scale;
				node->nod_flags |= nod_quad;
#ifdef NATIVE_QUAD
				return;
#endif

			default:
				fb_assert(false);
				// fall into

			case dtype_sql_time:
			case dtype_sql_date:
			case dtype_timestamp:
			case dtype_blob:
			case dtype_array:
			case dtype_dbkey:
				break;	// break to error reporting code
		}
	}

	if (desc->dsc_dtype == dtype_quad)
		IBERROR(224);	// msg 224 quad word arithmetic not supported

	ERR_post(Arg::Gds(isc_datype_notsup));	// data type not supported for arithmetic
}

ValueExprNode* SumAggNode::copy(thread_db* tdbb, NodeCopier& copier)
{
	SumAggNode* node = FB_NEW(*tdbb->getDefaultPool()) SumAggNode(*tdbb->getDefaultPool(),
		distinct, dialect1);
	node->arg = copier.copy(tdbb, arg);
	return node;
}

void SumAggNode::aggInit(thread_db* tdbb, jrd_req* request) const
{
	AggNode::aggInit(tdbb, request);

	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (dialect1)
		impure->make_long(0);
	else
	{
		// Initialize the result area as an int64. If the field being aggregated is approximate
		// numeric, the first call to add will convert the descriptor to double.
		impure->make_int64(0, node->nod_scale);
	}
}

void SumAggNode::aggPass(thread_db* /*tdbb*/, jrd_req* request, dsc* desc) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);
	++impure->vlux_count;

	if (dialect1)
		ArithmeticNode::add(desc, impure, node, blr_add);
	else
		ArithmeticNode::add2(desc, impure, node, blr_add);
}

dsc* SumAggNode::aggExecute(thread_db* /*tdbb*/, jrd_req* request) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (!impure->vlux_count)
		return NULL;

	return &impure->vlu_desc;
}

AggNode* SumAggNode::dsqlCopy(DsqlCompilerScratch* dsqlScratch) const
{
	return FB_NEW(getPool()) SumAggNode(getPool(), distinct, dialect1,
		PASS1_node(dsqlScratch, dsqlArg));
}


//--------------------


static AggNode::Register<MaxMinAggNode> maxAggInfo("MAX", blr_agg_max);
static AggNode::Register<MaxMinAggNode> minAggInfo("MIN", blr_agg_min);

MaxMinAggNode::MaxMinAggNode(MemoryPool& pool, MaxMinType aType, dsql_nod* aArg)
	: AggNode(pool, (aType == MaxMinAggNode::TYPE_MAX ? maxAggInfo : minAggInfo), false, false, aArg),
	  type(aType)
{
}

DmlNode* MaxMinAggNode::parse(thread_db* tdbb, MemoryPool& pool, CompilerScratch* csb, UCHAR blrOp)
{
	MaxMinAggNode* node = FB_NEW(pool) MaxMinAggNode(pool,
		(blrOp == blr_agg_max ? TYPE_MAX : TYPE_MIN));
	node->arg = PAR_parse_node(tdbb, csb, VALUE);
	return node;
}

void MaxMinAggNode::make(DsqlCompilerScratch* dsqlScratch, dsql_nod* /*thisNode*/, dsc* desc)
{
	MAKE_desc(dsqlScratch, desc, dsqlArg);
	desc->setNullable(true);
}

void MaxMinAggNode::getDesc(thread_db* tdbb, CompilerScratch* csb, dsc* desc)
{
	CMP_get_desc(tdbb, csb, arg, desc);
}

ValueExprNode* MaxMinAggNode::copy(thread_db* tdbb, NodeCopier& copier)
{
	MaxMinAggNode* node = FB_NEW(*tdbb->getDefaultPool()) MaxMinAggNode(*tdbb->getDefaultPool(),
		type);
	node->arg = copier.copy(tdbb, arg);
	return node;
}

void MaxMinAggNode::aggInit(thread_db* tdbb, jrd_req* request) const
{
	AggNode::aggInit(tdbb, request);

	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);
	impure->vlu_desc.dsc_dtype = 0;
}

void MaxMinAggNode::aggPass(thread_db* tdbb, jrd_req* request, dsc* desc) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	++impure->vlux_count;
	if (!impure->vlu_desc.dsc_dtype)
	{
		EVL_make_value(tdbb, desc, impure);
		return;
	}

	const SLONG result = MOV_compare(desc, &impure->vlu_desc);

	if ((type == TYPE_MAX && result > 0) || (type == TYPE_MIN && result < 0))
		EVL_make_value(tdbb, desc, impure);
}

dsc* MaxMinAggNode::aggExecute(thread_db* /*tdbb*/, jrd_req* request) const
{
	impure_value_ex* impure = request->getImpure<impure_value_ex>(node->nod_impure);

	if (!impure->vlux_count)
		return NULL;

	return &impure->vlu_desc;
}

AggNode* MaxMinAggNode::dsqlCopy(DsqlCompilerScratch* dsqlScratch) const
{
	return FB_NEW(getPool()) MaxMinAggNode(getPool(), type, PASS1_node(dsqlScratch, dsqlArg));
}


}	// namespace Jrd
