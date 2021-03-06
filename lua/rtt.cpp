/*
 * Lua-RTT bindings
 *
 * (C) Copyright 2010 Markus Klotzbuecher
 * markus.klotzbuecher@mech.kuleuven.be
 * Department of Mechanical Engineering,
 * Katholieke Universiteit Leuven, Belgium.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * version 2 of the License.
 *
 * As a special exception, you may use this file as part of a free
 * software library without restriction.  Specifically, if other files
 * instantiate templates or use macros or inline functions from this
 * file, or you compile this file and link it with other files to
 * produce an executable, this file does not by itself cause the
 * resulting executable to be covered by the GNU General Public
 * License.  This exception does not however invalidate any other
 * reasons why the executable file might be covered by the GNU General
 * Public License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 */

#include "rtt.hpp"

using namespace std;
using namespace RTT;
using namespace RTT::detail;
using namespace RTT::base;
using namespace RTT::internal;

static TaskContext* __getTC(lua_State*);

#define DEBUG

#ifdef DEBUG
# define _DBG(fmt, args...) printf("%s:%d\t" fmt "\n", __FUNCTION__, __LINE__, ##args)
#else
# define _DBG(fmt, args...) do { } while(0);
#endif

/*
 * Inspired by tricks from here: http://lua-users.org/wiki/DoItYourselfCppBinding
 */

/* overloading new */
void* operator new(size_t size, lua_State* L, const char* mt)
{
	void* ptr = lua_newuserdata(L, size);
	luaL_getmetatable(L, mt);
	/* assert(lua_istable(L, -1)) */  /* if you're paranoid */
	lua_setmetatable(L, -2);
	return ptr;
}

/*
 * luaM_pushobject_mt(L, "Port", InputPortInterface )(ctr_arg,...)
 * expands to
 * new(L, "Port") InputPortInterface(ctr_arg,...)
 */

#define luaM_pushobject(L, T) new(L, #T) T
#define luaM_pushobject_mt(L, MT, T) new(L, MT) T

/* return udata ptr or fail if wrong metatable */
#define luaM_checkudata(L, pos, T) reinterpret_cast<T*>(luaL_checkudata((L), (pos), #T))
#define luaM_checkudata_mt(L, pos, MT, T) reinterpret_cast<T*>(luaL_checkudata((L), (pos), MT))

/* return udata ptr or NULL if wrong metatable */
#define luaM_testudata(L, pos, T) (T*) (luaL_testudata((L), (pos), #T))
#define luaM_testudata_mt(L, pos, MT, T) (T*) (luaL_testudata((L), (pos), MT))

/*
 * boxed variants
 */

/* return boxed udata ptr or fail if wrong metatable */
#define luaM_checkudata_bx(L, pos, T) (T**) (luaL_checkudata((L), (pos), #T))
#define luaM_checkudata_mt_bx(L, pos, MT, T) (T**) (luaL_checkudata((L), (pos), MT))

/* return udata ptr or NULL if wrong metatable */
#define luaM_testudata_bx(L, pos, T) (T**) (luaL_testudata((L), (pos), #T))
#define luaM_testudata_mt_bx(L, pos, MT, T) (T**) (luaL_testudata((L), (pos), MT))

/* generate a function to push boxed pointers to lua */
#define gen_push_bxptr(name, MT, T)			   \
static void name(lua_State *L, T* ptr)		   	   \
{							   \
	T** ptrptr = (T**) lua_newuserdata(L, sizeof(T*)); \
	*ptrptr = ptr;					   \
	luaL_getmetatable(L, MT);			   \
	lua_setmetatable(L, -2);			   \
}							   \

/* template for generating GC function */
template<typename T>
int GCMethod(lua_State* L)
{
	reinterpret_cast<T*>(lua_touserdata(L, 1))->~T();
	return 0;
}


/***************************************************************
 * Some generic helpers
 ***************************************************************/

/* test if userdata on position ud has metatable tname */
void* luaL_testudata (lua_State *L, int ud, const char *tname)
{
	void *p = lua_touserdata(L, ud);

	if (p == NULL)
		goto out;

	if (!lua_getmetatable(L, ud))  {
		p = NULL;
		goto out;
	}

	/* it has a MT, is it the right one? */
	lua_getfield(L, LUA_REGISTRYINDEX, tname);
	if (!lua_rawequal(L, -1, -2))
		p = NULL;

	lua_pop(L, 2);  /* remove both metatables */
 out:
	return p;
}


void push_vect_str(lua_State *L, const std::vector<std::string> &v)
{
	int key = 1;
	lua_createtable(L, v.size(), 0);

	for(vector<std::string>::const_iterator it = v.begin(); it != v.end(); ++it) {
		lua_pushstring(L, it->c_str());
		lua_rawseti(L, -2, key++);
	}
}

/* forw decl */
static DataSourceBase::shared_ptr Variable_fromlua(lua_State *L, const char* type, int valind);

/***************************************************************
 * Variable (DataSourceBase)
 ***************************************************************/

/* helper, check if a variable is basic, that is _tolua will succeed */
static bool __Variable_isbasic(DataSourceBase::shared_ptr dsb)
{
	const std::string type = dsb->getTypeName();
	if(type == "bool" || type == "float" || type == "double" || type == "uint" ||
	   type == "int" || type == "char" || type == "string" || type == "void" )
		return true;
	else
		return false;
}


/*
 * converts a DataSourceBase to the corresponding Lua value and pushes
 * that on the stack.
 */
static int __Variable_tolua(lua_State *L, DataSourceBase::shared_ptr dsb)
{
	DataSourceBase *ds = dsb.get();
	assert(ds);
	const std::string type = dsb->getTypeName();

	if(type=="bool") {
		DataSource<bool>* dsb = DataSource<bool>::narrow(ds);
		if(dsb) lua_pushboolean(L, dsb->get());
		else goto out_nodsb;
	} else if (type == "float") {
		DataSource<float>* dsb = DataSource<float>::narrow(ds);
		if(dsb) lua_pushnumber(L, ((lua_Number) dsb->get()));
		else goto out_nodsb;
	} else if (type == "double") {
		DataSource<double>* dsb = DataSource<double>::narrow(ds);
		if(dsb) lua_pushnumber(L, ((lua_Number) dsb->get()));
		else goto out_nodsb;
	} else if (type == "uint") {
		DataSource<unsigned int>* dsb = DataSource<unsigned int>::narrow(ds);
		if(dsb) lua_pushnumber(L, ((lua_Number) dsb->get()));
		else goto out_nodsb;
	} else if (type == "int") {
		DataSource<int>* dsb = DataSource<int>::narrow(ds);
		if(dsb) lua_pushnumber(L, ((lua_Number) dsb->get()));
		else goto out_nodsb;
	} else if (type == "char") {
		DataSource<char>* dsb = DataSource<char>::narrow(ds);
		char c = dsb->get();
		if(dsb) lua_pushlstring(L, &c, 1);
		else goto out_nodsb;
	} else if (type == "string") {
		DataSource<std::string>* dsb = DataSource<std::string>::narrow(ds);
		if(dsb) lua_pushlstring(L, dsb->get().c_str(), dsb->get().size());
		else goto out_nodsb;
	} else if(type=="void") {
		DataSource<void>* dsb = DataSource<void>::narrow(ds);
		if(dsb) lua_pushnil(L);
		else goto out_nodsb;
	} else {
		goto out_conv_err;
	}

	/* all ok */
	return 1;

 out_conv_err:
	luaL_error(L, "Variable.tolua: can't convert type %s", type.c_str());
	return 0;

 out_nodsb:
	luaL_error(L, "Variable.tolua: narrow failed for %s Variable", type.c_str());
	return 0;
}

static int Variable_tolua(lua_State *L)
{
	DataSourceBase::shared_ptr dsb = *(luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr));
	return __Variable_tolua(L, dsb);
}

/*
 * this function takes a dsb and either pushes it as a Lua type if the
 * dsb is basic or otherwise as at Variable
 */
static void Variable_push_coerce(lua_State *L, DataSourceBase::shared_ptr dsb)
{
	if (__Variable_isbasic(dsb))
		__Variable_tolua(L, dsb);
	else
		luaM_pushobject_mt(L, "Variable", DataSourceBase::shared_ptr)(dsb);

}

static int Variable_getTypes(lua_State *L)
{
	push_vect_str(L, Types()->getTypes());
	return 1;
}

static int Variable_getMemberNames(lua_State *L)
{
	DataSourceBase::shared_ptr *dsbp = luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr);
	push_vect_str(L, (*dsbp)->getMemberNames());
	return 1;
}

static int Variable_getMember(lua_State *L)
{
	DataSourceBase::shared_ptr *dsbp = luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr);
	DataSourceBase::shared_ptr memdsb;
	const char *mem = luaL_checkstring(L, 2);

	memdsb = (*dsbp)->getMember(mem);

	if(memdsb == 0)
		lua_pushnil(L);
	else
		Variable_push_coerce(L, memdsb);
	return 1;
}

static int Variable_update(lua_State *L)
{
	int ret;
	DataSourceBase::shared_ptr dsb;
	DataSourceBase::shared_ptr *dsbp;
	DataSourceBase::shared_ptr self = *(luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr));

	if ((dsbp = luaM_testudata_mt(L, 2, "Variable", DataSourceBase::shared_ptr)) != NULL)
		dsb = *dsbp;
	else
		dsb = Variable_fromlua(L, self->getType().c_str(), 2);

	ret = self->update(dsb.get());
	lua_pushboolean(L, ret);
	return 1;
}

/* create variable */
static int Variable_create(lua_State *L)
{
	const char *type;
	type = luaL_checkstring(L, 1);

	if(!strcmp(type, "void"))
		luaL_error(L, "Variable.new: can't create void variable");

	TypeInfo* ti = Types()->type(type);

	if(ti==0)
		luaL_error(L, "Variable.new: unknown type %s", type);

	luaM_pushobject_mt(L, "Variable", DataSourceBase::shared_ptr)(ti->buildValue());
	return 1;
}

/* try to convert lua value on stack at valind to DSB of type */
static DataSourceBase::shared_ptr Variable_fromlua(lua_State *L, const char* type, int valind)
{
	DataSourceBase::shared_ptr dsb;
	luaL_checkany(L, valind);
	int luatype = lua_type(L, valind); 	/* type of lua variable */

	if(strcmp(type, "bool") == 0) {
		lua_Number x;
		if(luatype == LUA_TBOOLEAN)
			x = (lua_Number) lua_toboolean(L, valind);
		else if (luatype == LUA_TNUMBER)
			x = lua_tonumber(L, valind);
		else
			goto out_conv_err;

		dsb = new ValueDataSource<bool>((bool) x);

	} else if (strcmp(type, "int") == 0) {
		lua_Number x;
		if (luatype == LUA_TNUMBER)
			x = lua_tonumber(L, valind);
		else
			goto out_conv_err;

		dsb = new ValueDataSource<int>(x); // (int)

	} else if (strcmp(type, "uint") == 0) {
		lua_Number x;
		if (luatype == LUA_TNUMBER)
			x = lua_tonumber(L, valind);
		else
			goto out_conv_err;

		dsb = new ValueDataSource<unsigned int>((unsigned int) x);

	} else if (strcmp(type, "double") == 0) {
		lua_Number x;
		if (luatype == LUA_TNUMBER)
			x = lua_tonumber(L, valind);
		else
			goto out_conv_err;

		dsb = new ValueDataSource<double>((double) x);

	} else if (strcmp (type, "float") == 0) {
		lua_Number x;
		if (luatype == LUA_TNUMBER)
			x = lua_tonumber(L, valind);
		else
			goto out_conv_err;

		dsb = new ValueDataSource<float>((float) x);

	} else if (strcmp(type, "char") == 0) {
		const char *x;
		size_t l;
		if (luatype == LUA_TSTRING)
			x = lua_tolstring(L, valind, &l);
		else
			goto out_conv_err;

		dsb = new ValueDataSource<char>(x[0]);

	} else if (strcmp(type, "string") == 0) {
		// std::string x;
		const char *x;
		if (luatype == LUA_TSTRING)
			x = lua_tostring(L, valind); /* nonhrt */
		else
			goto out_conv_err;

		dsb = new ValueDataSource<std::string>(x);

	} else if (luatype == LUA_TNUMBER) { /* last resort, try conversion via double */
		TypeInfo* ti = Types()->type(type);
		DataSourceBase::shared_ptr double_dsb = Variable_fromlua(L, "double", valind);
		dsb = ti->convert(double_dsb);
	} else {
		goto out_conv_err;
	}

	/* everybody happy */
	return dsb;

 out_conv_err:
	luaL_error(L, "__lua_todsb: can't convert lua %s to %s variable", lua_typename(L, luatype), type);
	return NULL;
}


static int Variable_create_ival(lua_State *L, int typeind, int valind)
{
	DataSourceBase::shared_ptr dsb;
	luaL_checkany(L, valind);
	const char* type = luaL_checkstring(L, typeind);	/* target dsb type */
	dsb = Variable_fromlua(L, type, valind);
	luaM_pushobject_mt(L, "Variable", DataSourceBase::shared_ptr)(dsb);
	return 1;
}

static int Variable_new(lua_State *L)
{
	int argc = lua_gettop(L);
	if(argc == 1)
		return Variable_create(L);
	else if(argc == 2)
		return Variable_create_ival(L, 1, 2);
	else
		luaL_error(L, "Variable.new: invalid number of args");

	return 0;
}

static int Variable_toString(lua_State *L)
{
	DataSourceBase::shared_ptr *dsbp = luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr);
	lua_pushstring(L, ((*dsbp)->toString()).c_str());
	return 1;
}

static int Variable_getType(lua_State *L)
{
	DataSourceBase::shared_ptr *dsbp = luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr);
	lua_pushstring(L, (*dsbp)->getType().c_str());
	return 1;
}

static int Variable_getTypeName(lua_State *L)
{
	DataSourceBase::shared_ptr *dsbp = luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr);
	lua_pushstring(L, (*dsbp)->getTypeName().c_str());
	return 1;
}

static int Variable_resize(lua_State *L)
{
	int size;
	DataSourceBase::shared_ptr *dsbp = luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr);
	size = luaL_checknumber(L, 2);
	const TypeInfo *ti = (*dsbp)->getTypeInfo();
	lua_pushboolean(L, ti->resize(*dsbp, size));
	return 1;
}


/*
 * Operators
 */
static int Variable_unm(lua_State *L)
{
	types::OperatorRepository::shared_ptr opreg = types::OperatorRepository::Instance();
	DataSourceBase::shared_ptr arg = *(luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr));
	DataSourceBase::shared_ptr res = opreg->applyUnary("-", arg.get());
	luaM_pushobject_mt(L, "Variable", DataSourceBase::shared_ptr)(res);
	return 1;
}


/* don't try this at home */
#define gen_opmet(name, op)					\
static int name(lua_State *L)					\
{								\
	DataSourceBase::shared_ptr arg1 = *(luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr)); \
	DataSourceBase::shared_ptr arg2 = *(luaM_checkudata_mt(L, 2, "Variable", DataSourceBase::shared_ptr)); \
	types::OperatorRepository::shared_ptr opreg = types::OperatorRepository::Instance(); \
	DataSourceBase *res = opreg->applyBinary(#op, arg1.get(), arg2.get()); \
	if(res == 0)							\
		luaL_error(L , "%s (operator %s) failed", #name, #op);	\
	res->evaluate();						\
	luaM_pushobject_mt(L, "Variable", DataSourceBase::shared_ptr)(res);	\
	return 1;							\
}									\

gen_opmet(Variable_add, +)
gen_opmet(Variable_sub, -)
gen_opmet(Variable_mul, *)
gen_opmet(Variable_div, /)
gen_opmet(Variable_mod, %)
gen_opmet(Variable_pow, ^)

/* these flavors convert the boolean return dsb to a lua bool */
#define gen_opmet_bool(name, op)				\
static int name(lua_State *L)					\
{								\
	DataSourceBase::shared_ptr arg1 = *(luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr)); \
	DataSourceBase::shared_ptr arg2 = *(luaM_checkudata_mt(L, 2, "Variable", DataSourceBase::shared_ptr)); \
	types::OperatorRepository::shared_ptr opreg = types::OperatorRepository::Instance(); \
	DataSourceBase *res = opreg->applyBinary(#op, arg1.get(), arg2.get()); \
	if(res == 0)							\
		luaL_error(L , "%s (operator %s) failed", #name, #op);	\
	res->evaluate();						\
	return __Variable_tolua(L, res);				\
}									\

gen_opmet_bool(Variable_eq, ==)
gen_opmet_bool(Variable_lt, <)
gen_opmet_bool(Variable_le, <=)

static int Variable_opBinary(lua_State *L)
{
	types::OperatorRepository::shared_ptr opreg = types::OperatorRepository::Instance();
	const char *op = luaL_checkstring(L, 1);
	DataSourceBase::shared_ptr arg1 = *(luaM_checkudata_mt(L, 2, "Variable", DataSourceBase::shared_ptr));
	DataSourceBase::shared_ptr arg2 = *(luaM_checkudata_mt(L, 3, "Variable", DataSourceBase::shared_ptr));
	DataSourceBase *res;

	res = opreg->applyBinary(op, arg1.get(), arg2.get());
	if(res == 0)
		luaL_error(L , "Variable.opBinary '%s' not applicable to args", op);

	res->evaluate();

	luaM_pushobject_mt(L, "Variable", DataSourceBase::shared_ptr)(res);
	return 1;
}

/*
 * this is a dispatcher which checks if the key is a method, otherwise
 * calls get for looking up the field. Inspired by
 * http://lua-users.org/wiki/ObjectProperties
 */
static int Variable_index(lua_State *L)
{
	const char* key = luaL_checkstring(L, 2);

	lua_getmetatable(L, 1);
	lua_getfield(L, -1, key);

	/* Either key is name of a method in the metatable */
	if(!lua_isnil(L, -1))
		return 1;

	/* ... or its a field access, so recall as self.get(self, value). */
	lua_settop(L, 2);
	return Variable_getMember(L);
}

static int Variable_newindex(lua_State *L)
{
	DataSourceBase::shared_ptr *newvalp;
	DataSourceBase::shared_ptr newval;
	DataSourceBase::shared_ptr master = *(luaM_checkudata_mt(L, 1, "Variable", DataSourceBase::shared_ptr));
	const char* member = luaL_checkstring(L, 2);

	/* get dsb to be updated: we need its type before get-or-create'ing arg3 */
	types::OperatorRepository::shared_ptr opreg = types::OperatorRepository::Instance();
	DataSourceBase::shared_ptr curval = master->getMember(member);

	if (curval == 0)
		luaL_error(L, "Variable.newindex: indexing failed, no member %s", member);

	if ((newvalp = luaM_testudata_mt(L, 3, "Variable", DataSourceBase::shared_ptr)) != NULL)
		newval = *newvalp;
	else
		newval = Variable_fromlua(L, curval->getType().c_str(), 3);

	lua_pushboolean(L, curval->update(newval.get()));
	return 1;
}

static const struct luaL_Reg Variable_f [] = {
	{ "new", Variable_new },
	{ "tolua", Variable_tolua },
	{ "toString", Variable_toString },
	{ "getTypes", Variable_getTypes },
	{ "getType", Variable_getType },
	{ "getTypeName", Variable_getTypeName },
	{ "getMemberNames", Variable_getMemberNames },
	{ "getMember", Variable_getMember },
	{ "resize", Variable_resize },
	{ "opBinary", Variable_opBinary },
	{ "assign", Variable_update }, /* assign seems a better name than update */
	{ "unm", Variable_unm },
	{ "add", Variable_add },
	{ "sub", Variable_sub },
	{ "mul", Variable_mul },
	{ "div", Variable_div },
	{ "mod", Variable_mod },
	{ "pow", Variable_pow },
	{ "eq", Variable_eq },
	{ "lt", Variable_lt },
	{ "le", Variable_le },
	{ NULL, NULL}
};

static const struct luaL_Reg Variable_m [] = {
	{ "tolua", Variable_tolua },
	{ "toString", Variable_toString },
	{ "getType", Variable_getType },
	{ "getTypeName", Variable_getTypeName },
	{ "getMemberNames", Variable_getMemberNames },
	{ "getMember", Variable_getMember },
	{ "resize", Variable_resize },
	{ "opBinary", Variable_opBinary },
	{ "assign", Variable_update }, /* assign seems a better name than update */
	{ "__unm", Variable_unm },
	{ "__add", Variable_add },
	{ "__sub", Variable_sub },
	{ "__mul", Variable_mul },
	{ "__div", Variable_div },
	{ "__mod", Variable_mod },
	{ "__pow", Variable_pow },
	{ "__eq", Variable_eq },
	{ "__lt", Variable_lt },
	{ "__le", Variable_le },
	{ "__index", Variable_index },
	{ "__newindex", Variable_newindex },
	{ "__gc", GCMethod<DataSourceBase::shared_ptr> },
	// {"__gc", Variable_gc},
	{ NULL, NULL}
};


/***************************************************************
 * Property (boxed)
 ***************************************************************/

gen_push_bxptr(Property_push, "Property", PropertyBase)

static int Property_new(lua_State *L)
{
	const char *type, *name, *desc;
	PropertyBase *pb;
	int argc = lua_gettop(L);
	type = luaL_checkstring(L, 1);

	/* name and description are optional */
	name = (argc > 1) ? luaL_checkstring(L, 2) : "";
	desc = (argc > 2) ? luaL_checkstring(L, 3) : "";

	types::TypeInfo *ti = types::TypeInfoRepository::Instance()->type(type);

	if(!ti)
		luaL_error(L, "Property.new: unknown type %s", type);

	pb =  ti->buildProperty(name, desc);
	Property_push(L, pb);
	return 1;
}

static int Property_get(lua_State *L)
{
	PropertyBase *pb = *(luaM_checkudata_mt_bx(L, 1, "Property", PropertyBase));
	Variable_push_coerce(L, pb->getDataSource());
	return 1;
}

static int Property_set(lua_State *L)
{
	DataSourceBase::shared_ptr newdsb;
	DataSourceBase::shared_ptr *newdsbp;
	PropertyBase *pb = *(luaM_checkudata_mt_bx(L, 1, "Property", PropertyBase));

	if ((newdsbp = luaM_testudata_mt(L, 2, "Variable", DataSourceBase::shared_ptr)) != NULL)
		newdsb = *newdsbp;
	else
		newdsb = Variable_fromlua(L, pb->getTypeInfo()->getTypeName().c_str(), 2);

	DataSourceBase::shared_ptr propdsb = pb->getDataSource();
	propdsb->update(newdsb.get());
	return 1;
}

static int Property_info(lua_State *L)
{
	PropertyBase *pb = *(luaM_checkudata_mt_bx(L, 1, "Property", PropertyBase));
	lua_newtable(L);
	lua_pushstring(L, "name"); lua_pushstring(L, pb->getName().c_str()); lua_rawset(L, -3);
	lua_pushstring(L, "desc"); lua_pushstring(L, pb->getDescription().c_str()); lua_rawset(L, -3);
	lua_pushstring(L, "type"); lua_pushstring(L, pb->getType().c_str()); lua_rawset(L, -3);
	return 1;
}

#if NOT_USED_YET
/*
 * Race condition if we collect properties: if we add this property to
 * a TC and our life ends before that of the TC, the property will be
 * deleted before the TaskContext.
 */
static int Property_gc(lua_State *L)
{
	PropertyBase *pb = *(luaM_checkudata_mt_bx(L, 1, "Property", PropertyBase));
	delete pb;
	return 0;
}
#endif

/* only explicit destruction allowed */
static int Property_del(lua_State *L)
{
	PropertyBase *pb = *(luaM_checkudata_mt_bx(L, 1, "Property", PropertyBase));
	delete pb;

	/* this prevents calling rtt methods which would cause a crash */
	luaL_getmetatable(L, "__dead__");
	lua_setmetatable(L, -2);
	return 0;
}

/* indexability of properties */
/*
 * this is a dispatcher which checks if the key is a method, otherwise
 * calls get for looking up the field. Inspired by
 * http://lua-users.org/wiki/ObjectProperties
 */
static int Property_index(lua_State *L)
{
	const char* key = luaL_checkstring(L, 2);

	lua_getmetatable(L, 1);
	lua_getfield(L, -1, key); /* this actually calls the method */

	/* Either key is name of a method in the metatable */
	if(!lua_isnil(L, -1))
		return 1;

	lua_settop(L, 2); 	/* reset stack */
	Property_get(L);	/* pushes property var */
	lua_replace(L, 1);	/* replace prop with var */
	return Variable_index(L);
}

static int Property_newindex(lua_State *L)
{
	Property_get(L);
	lua_replace(L, 1);
	return Variable_newindex(L);
}

static const struct luaL_Reg Property_f [] = {
	{"new", Property_new },
	{"get", Property_get },
	{"set", Property_set },
	{"info", Property_info },
	{"delete", Property_del },
	{NULL, NULL}
};

static const struct luaL_Reg Property_m [] = {
	{"get", Property_get },
	{"set", Property_set },
	{"info", Property_info },
	// todo: shall we or not? s.o. {"__gc", Property_gc },
	{"delete", Property_del },
	{"__index", Property_index },
	{"__newindex", Property_newindex },
	{NULL, NULL}
};

/***************************************************************
 * Ports (boxed)
 ***************************************************************/

/* both input or output */
static int Port_info(lua_State *L)
{
	int arg_type;
	const char* port_type = NULL;
	PortInterface **pip;
	PortInterface *pi = NULL;

	if((pip = (PortInterface**) luaL_testudata(L, 1, "InputPort")) != NULL) {
		pi = *pip;
		port_type = "in";
	} else if((pip = (PortInterface**) luaL_testudata(L, 1, "OutputPort")) != NULL) {
		pi = *pip;
		port_type = "out";
	}
	else {
		arg_type = lua_type(L, 1);
		luaL_error(L, "Port.info: invalid argument, expected Port, got %s",
			   lua_typename(L, arg_type));
	}

	lua_newtable(L);
	lua_pushstring(L, "name"); lua_pushstring(L, pi->getName().c_str()); lua_rawset(L, -3);
	lua_pushstring(L, "desc"); lua_pushstring(L, pi->getDescription().c_str()); lua_rawset(L, -3);
	lua_pushstring(L, "connected"); lua_pushboolean(L, pi->connected()); lua_rawset(L, -3);
	lua_pushstring(L, "isLocal"); lua_pushboolean(L, pi->isLocal()); lua_rawset(L, -3);
	lua_pushstring(L, "type"); lua_pushstring(L, pi->getTypeInfo()->getTypeName().c_str()); lua_rawset(L, -3);
	lua_pushstring(L, "porttype"); lua_pushstring(L, port_type); lua_rawset(L, -3);

	return 1;
}

static int Port_connect(lua_State *L)
{
	int arg_type, ret;
	PortInterface **pip1, **pip2;
	PortInterface *pi1 = NULL;
	PortInterface *pi2 = NULL;

	if((pip1 = (PortInterface**) luaL_testudata(L, 1, "InputPort")) != NULL) {
		pi1= *pip1;
	} else if((pip1 = (PortInterface**) luaL_testudata(L, 1, "OutputPort")) != NULL) {
		pi1= *pip1;
	}
	else {
		arg_type = lua_type(L, 1);
		luaL_error(L, "Port.info: invalid argument 1, expected Port, got %s",
			   lua_typename(L, arg_type));
	}
	if((pip2 = (PortInterface**) luaL_testudata(L, 2, "InputPort")) != NULL) {
		pi2= *pip2;
	} else if((pip2 = (PortInterface**) luaL_testudata(L, 2, "OutputPort")) != NULL) {
		pi2= *pip2;
	}
	else {
		arg_type = lua_type(L, 2);
		luaL_error(L, "Port.connect: invalid argument 2, expected Port, got %s",
			   lua_typename(L, arg_type));
	}

	ret = pi1->connectTo(pi2);
	lua_pushboolean(L, ret);

	return 1;
}



/* InputPort (boxed) */

gen_push_bxptr(InputPort_push, "InputPort", InputPortInterface)

static int InputPort_new(lua_State *L)
{
	const char *type, *name, *desc;
	InputPortInterface* ipi;
	int argc = lua_gettop(L);

	type = luaL_checkstring(L, 1);

	/* name and description are optional */
	name = (argc > 1) ? luaL_checkstring(L, 2) : "";
	desc = (argc > 2) ? luaL_checkstring(L, 3) : "";

	types::TypeInfo *ti = types::TypeInfoRepository::Instance()->type(type);
	if(ti==0)
		luaL_error(L, "InputPort.new: unknown type %s", type);

	ipi = ti->inputPort(name);
	ipi->doc(desc);
	InputPort_push(L, ipi);
	return 1;
}

static int InputPort_read(lua_State *L)
{
	int ret = 1;
	InputPortInterface *ip = *(luaM_checkudata_mt_bx(L, 1, "InputPort", InputPortInterface));
	DataSourceBase::shared_ptr dsb;
	DataSourceBase::shared_ptr *dsbp;
	FlowStatus fs;

	/* if we get don't get a DS to store the result, create one */
	if ((dsbp = luaM_testudata_mt(L, 2, "Variable", DataSourceBase::shared_ptr)) != NULL)
		dsb = *dsbp;
	else {
		dsb = ip->getTypeInfo()->buildValue();
		ret = 2;
	}

	fs = ip->read(dsb);

	if(fs == NoData) lua_pushstring(L, "NoData");
	else if (fs == NewData) lua_pushstring(L, "NewData");
	else if (fs == OldData) lua_pushstring(L, "OldData");
	else luaL_error(L, "InputPort.read: unknown FlowStatus returned");

	if(ret>1)
		Variable_push_coerce(L, dsb);

	return ret;
}

#ifdef NOT_USED_YET
static int InputPort_gc(lua_State *L)
{
	InputPortInterface *ip = *(luaM_checkudata_mt_bx(L, 1, "InputPort", InputPortInterface));
	delete ip;
 	return 0;
}
#endif

/* only explicit destruction allowed */
static int InputPort_del(lua_State *L)
{
	InputPortInterface *ip = *(luaM_checkudata_mt_bx(L, 1, "InputPort", InputPortInterface));
	delete ip;

	/* this prevents calling rtt methods which would cause a crash */
	luaL_getmetatable(L, "__dead__");
	lua_setmetatable(L, -2);
	return 0;
}

static const struct luaL_Reg InputPort_f [] = {
	{"new", InputPort_new },
	{"read", InputPort_read },
	{"info", Port_info },
	{"connect", Port_connect },
	{"delete", InputPort_del },
	{NULL, NULL}
};

static const struct luaL_Reg InputPort_m [] = {
	{"read", InputPort_read },
	{"info", Port_info },
	{"delete", InputPort_del },
	{"connect", Port_connect },
	/* {"__gc", InputPort_gc }, */
	{NULL, NULL}
};

/* OutputPort */

gen_push_bxptr(OutputPort_push, "OutputPort", OutputPortInterface)


static int OutputPort_new(lua_State *L)
{
	const char *type, *name, *desc;
	OutputPortInterface* opi;
	int argc = lua_gettop(L);

	type = luaL_checkstring(L, 1);

	/* name and description are optional */
	name = (argc > 1) ? luaL_checkstring(L, 2) : "";
	desc = (argc > 2) ? luaL_checkstring(L, 3) : "";

	types::TypeInfo *ti = types::TypeInfoRepository::Instance()->type(type);

	if(ti==0)
		luaL_error(L, "OutputPort.new: unknown type %s", type);

	opi = ti->outputPort(name);
	opi->doc(desc);
	OutputPort_push(L, opi);
	return 1;
}

static int OutputPort_write(lua_State *L)
{
	DataSourceBase::shared_ptr dsb;
	DataSourceBase::shared_ptr *dsbp;

	OutputPortInterface *op = *(luaM_checkudata_mt_bx(L, 1, "OutputPort", OutputPortInterface));

	/* fastpath: Variable argument */
	if ((dsbp = luaM_testudata_mt(L, 2, "Variable", DataSourceBase::shared_ptr)) != NULL) {
		dsb = *dsbp;
	} else  {
		/* slowpath: convert lua value to dsb */
		std::string type = op->getTypeInfo()->getTypeName();
		dsb = Variable_fromlua(L, type.c_str(), 2);
	}
	op->write(dsb);
	return 0;
}

#ifdef NOT_USED_YET
static int OutputPort_gc(lua_State *L)
{
	OutputPortInterface *op = *(luaM_checkudata_mt_bx(L, 1, "OutputPort", OutputPortInterface));
	delete op;
	return 0;
}
#endif

/* only explicit destruction allowed */
static int OutputPort_del(lua_State *L)
{
	OutputPortInterface *op = *(luaM_checkudata_mt_bx(L, 1, "OutputPort", OutputPortInterface));
	delete op;

	/* this prevents calling rtt methods which would cause a crash */
	luaL_getmetatable(L, "__dead__");
	lua_setmetatable(L, -2);
	return 0;
}

static const struct luaL_Reg OutputPort_f [] = {
	{"new", OutputPort_new },
	{"write", OutputPort_write },
	{"info", Port_info },
	{"connect", Port_connect },
	{"delete", OutputPort_del },
	{NULL, NULL}
};

static const struct luaL_Reg OutputPort_m [] = {
	{"write", OutputPort_write },
	{"info", Port_info },
	{"connect", Port_connect },
	{"delete", OutputPort_del },
	/* {"__gc", OutputPort_gc }, */
	{NULL, NULL}
};

/***************************************************************
 * Operation (boxed)
 ***************************************************************/

gen_push_bxptr(Operation_push, "Operation", OperationInterfacePart)

static int Operation_info(lua_State *L)
{
	int i=1;
	OperationInterfacePart *oip = *(luaM_checkudata_mt_bx(L, 1, "Operation", OperationInterfacePart));
	std::vector<ArgumentDescription> args;

	lua_pushstring(L, oip->getName().c_str());	/* name */
	lua_pushstring(L, oip->description().c_str());	/* description */
	lua_pushstring(L, oip->resultType().c_str());	/* result type */
	lua_pushinteger(L, oip->arity());		/* arity */

	args = oip->getArgumentList();

	lua_newtable(L);

	for (std::vector<ArgumentDescription>::iterator it = args.begin(); it != args.end(); it++) {
		lua_newtable(L);
		lua_pushstring(L, "name"); lua_pushstring(L, it->name.c_str()); lua_rawset(L, -3);
		lua_pushstring(L, "type"); lua_pushstring(L, it->type.c_str()); lua_rawset(L, -3);
		lua_pushstring(L, "desc"); lua_pushstring(L, it->description.c_str()); lua_rawset(L, -3);
		lua_rawseti(L, -2, i++);
	}

	return 5;
}

static int __Operation_call(lua_State *L)
{
	std::vector<base::DataSourceBase::shared_ptr> args;
	DataSourceBase::shared_ptr dsb;
	DataSourceBase::shared_ptr *dsbp;
	DataSourceBase::shared_ptr ret, ret2;
	types::TypeInfo *ti;

	unsigned int argc = lua_gettop(L);
	TaskContext *this_tc = __getTC(L);
	OperationInterfacePart *oip = *(luaM_checkudata_mt_bx(L, 1, "Operation", OperationInterfacePart));

	if(oip->arity() != argc-1)
		luaL_error(L, "Operation.call: wrong number of args. expected %d, got %d", oip->arity(), argc-1);

	for(unsigned int arg=2; arg<=argc; arg++) {
		/* fastpath: Variable argument */
		if ((dsbp = luaM_testudata_mt(L, arg, "Variable", DataSourceBase::shared_ptr)) != NULL) {
			dsb = *dsbp;
		} else  {
			/* slowpath: convert lua value to dsb */
			std::string type = oip->getArgumentType(arg-1)->getTypeName().c_str();
			dsb = Variable_fromlua(L, type.c_str(), arg);
		}
		args.push_back(dsb);
	}

	ret = oip->produce(args, this_tc->engine());

	/* not so nice: construct a ValueDataSource for the return Value
	 * todo: at least avoid the type conversion to string.
	 */
	if(oip->resultType() != "void") {
		ti = types::TypeInfoRepository::Instance()->type(oip->resultType());
		if(!ti)
			luaL_error(L, "Operation.call: can't create return value DSB of type '%s'",
				   oip->resultType().c_str());
		ret2 = ti->buildValue();
		ret2->update(ret.get());
		Variable_push_coerce(L, ret2);
	} else {
		ret->evaluate();
		lua_pushnil(L);
	}
	return 1;
}

static int Operation_call(lua_State *L)
{
	int ret;
	try {
		ret = __Operation_call(L);
	} catch(...) {
		luaL_error(L, "Operation.call: caught exception");
	}
	return ret;
}

static int __Operation_send(lua_State *L)
{
	DataSourceBase::shared_ptr dsb, *dsbp;

	unsigned int argc = lua_gettop(L);
	TaskContext *this_tc = __getTC(L);
	OperationInterfacePart *oip = *(luaM_checkudata_mt_bx(L, 1, "Operation", OperationInterfacePart));

	OperationCallerC *occ = new OperationCallerC(oip, oip->getName(), this_tc->engine()); // todo: alloc on stack?

	if(oip->arity() != argc-1)
		luaL_error(L, "Operation.send: wrong number of args. expected %d, got %d",
			   oip->arity(), argc);

	for(unsigned int arg=2; arg<=argc; arg++) {
		/* fastpath: Variable argument */
		if ((dsbp = luaM_testudata_mt(L, arg, "Variable", DataSourceBase::shared_ptr)) != NULL) {
			dsb = *dsbp;
		} else  {
			/* slowpath: convert lua value to dsb */
			std::string type = oip->getArgumentType(arg-1)->getTypeName().c_str();
			dsb = Variable_fromlua(L, type.c_str(), arg);
		}
		occ->arg(dsb);
	}

	/* call send and construct push SendHandle userdata */
	luaM_pushobject_mt(L, "SendHandle", SendHandleC)(occ->send());
	return 1;
}

static int Operation_send(lua_State *L)
{
	int ret;
	try {
		ret = __Operation_send(L);
	} catch(...) {
		luaL_error(L, "Operation.send: caught exception");
	}
	return ret;
}


static const struct luaL_Reg Operation_f [] = {
	{ "info", Operation_info },
	{ "call", Operation_call },
	{ "send", Operation_send },
	{ NULL, NULL }

};

static const struct luaL_Reg Operation_m [] = {
	{ "info", Operation_info },
	{ "send", Operation_send },
	{ "__call", Operation_call },
	{ NULL, NULL }
};

/***************************************************************
 * Service (boxed)
 ***************************************************************/

static int Service_getName(lua_State *L)
{
	Service::shared_ptr srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	lua_pushstring(L, srv->getName().c_str());
	return 1;
}

static int Service_doc(lua_State *L)
{
	int ret;
	const char* doc;
	Service::shared_ptr srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	if(lua_gettop(L) == 1) {
		lua_pushstring(L, srv->doc().c_str());
		ret = 1;
	} else {
		doc = luaL_checkstring(L, 2);
		srv->doc(doc);
		ret = 0;
	}

	return ret;
}

static int Service_getProviderNames(lua_State *L)
{
	Service::shared_ptr srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	push_vect_str(L, srv->getProviderNames());
	return 1;
}

static int Service_getOperationNames(lua_State *L)
{
	Service::shared_ptr srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	push_vect_str(L, srv->getOperationNames());
	return 1;
}

static int Service_hasOperation(lua_State *L)
{
	int ret;
	Service::shared_ptr srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	const char* op = luaL_checkstring(L, 2);
	ret = srv->hasOperation(op);
	lua_pushboolean(L, ret);
	return 1;
}

static int Service_getPortNames(lua_State *L)
{
	Service::shared_ptr srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	push_vect_str(L, srv->getPortNames());
	return 1;
}

static int Service_provides(lua_State *L)
{
	int ret, i, argc;
	const char* subsrv_str;
	Service::shared_ptr srv, subsrv;

	srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	argc=lua_gettop(L);

	/* return "this" if no args given */
	if(argc == 1) {
		ret = 1;
		goto out;
	}

	for(i=2; i<=argc; i++) {
		subsrv_str = luaL_checkstring(L, i);
		// subsrv = srv->provides(subsrv_str);
		subsrv = srv->getService(subsrv_str);
		if (subsrv == 0)
			luaL_error(L, "Service.provides: no subservice %s of service %s",
				   srv->getName().c_str(), subsrv_str);
		else
			luaM_pushobject_mt(L, "Service", Service::shared_ptr)(subsrv);
	}
	ret = argc - 1;

 out:
	return ret;
}

static int Service_getOperation(lua_State *L)
{
	const char *op_str;
	OperationInterfacePart *oip;
	Service::shared_ptr srv;

	srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	op_str = luaL_checkstring(L, 2);
	oip = srv->getOperation(op_str);

	if(!oip)
		luaL_error(L, "Service_getOperation: service %s has no operation %s",
			   srv->getName().c_str(), op_str);
	Operation_push(L, oip);
	return 1;
}

static int Service_getPort(lua_State *L)
{
	const char* name;
	PortInterface *pi;
	InputPortInterface *ipi;
	OutputPortInterface *opi;

	Service::shared_ptr srv;

	srv = *(luaM_checkudata_mt(L, 1, "Service", Service::shared_ptr));
	name = luaL_checkstring(L, 2);

	pi = srv->getPort(name);
	if(!pi)
		luaL_error(L, "Service.getPort: service %s has no port %",
			   srv->getName().c_str(), name);

	/* input or output? */
	if ((ipi = dynamic_cast<InputPortInterface *> (pi)) != NULL)
		InputPort_push(L, ipi);
	else if ((opi = dynamic_cast<OutputPortInterface *> (pi)) != NULL)
		OutputPort_push(L, opi);
	else
		luaL_error(L, "Service.getPort: unknown port type returned");

	return 1;
}

static const struct luaL_Reg Service_f [] = {
	{ "getName", Service_getName },
	{ "doc", Service_doc },
	{ "getProviderNames", Service_getProviderNames },
	{ "getOperationNames", Service_getOperationNames },
	{ "hasOperation", Service_hasOperation },
	{ "getPortNames", Service_getPortNames },
	{ "provides", Service_provides },
	{ "getOperation", Service_getOperation },
	{ "getPort", Service_getPort },
	{ NULL, NULL }
};

static const struct luaL_Reg Service_m [] = {
	{ "getName", Service_getName },
	{ "doc", Service_doc },
	{ "getProviderNames", Service_getProviderNames },
	{ "getOperationNames", Service_getOperationNames },
	{ "hasOperation", Service_hasOperation },
	{ "getPortNames", Service_getPortNames },
	{ "provides", Service_provides },
	{ "getOperation", Service_getOperation },
	{ "getPort", Service_getPort },
	{ "__gc", GCMethod<Service::shared_ptr> },
	{ NULL, NULL }
};

/***************************************************************
 * ServiceRequester
 ***************************************************************/

gen_push_bxptr(ServiceRequester_push, "ServiceRequester", ServiceRequester)

static int ServiceRequester_getRequestName(lua_State *L)
{
	ServiceRequester *sr;

	sr = *(luaM_checkudata_bx(L, 1, ServiceRequester));
	lua_pushstring(L, sr->getRequestName().c_str());
	return 1;
}

static int ServiceRequester_getRequesterNames(lua_State *L)
{
	ServiceRequester *sr;
	sr = *(luaM_checkudata_bx(L, 1, ServiceRequester));
	push_vect_str(L, sr->getRequesterNames());
	return 1;
}

static int ServiceRequester_ready(lua_State *L)
{
	int ret;
	ServiceRequester *sr;
	sr = *(luaM_checkudata_bx(L, 1, ServiceRequester));
	ret = sr->ready();
	lua_pushboolean(L, ret);
	return 1;
}

static int ServiceRequester_disconnect(lua_State *L)
{
	ServiceRequester *sr;
	sr = *(luaM_checkudata_bx(L, 1, ServiceRequester));
	sr->disconnect();
	return 0;
}

static int ServiceRequester_requires(lua_State *L)
{
	int argc, ret, i;
	const char* subsr_str;
	ServiceRequester *sr;
	ServiceRequester *subsr;

	sr = *(luaM_checkudata_bx(L, 1, ServiceRequester));
	argc = lua_gettop(L);

	/* return "this" if no args given */
	if(argc == 1) {
		ret = 1;
		goto out;
	}

	for(i=2; i<=argc; i++) {
		subsr_str = luaL_checkstring(L, i);
		subsr = sr->requires(subsr_str);
		if (subsr == 0)
			luaL_error(L, "ServiceRequester: no required subservice %s of service %s",
				   subsr_str, sr->getRequestName().c_str());
		else
			ServiceRequester_push(L, subsr);
	}
	ret = argc - 1;

 out:
	return ret;
}

static const struct luaL_Reg ServiceRequester_f [] = {
	{ "getRequestName", ServiceRequester_getRequestName },
	{ "getRequesterNames", ServiceRequester_getRequesterNames },
	{ "ready", ServiceRequester_ready },
	{ "disconnect", ServiceRequester_disconnect },
	{ "requires", ServiceRequester_requires },
	{ NULL, NULL }
};

static const struct luaL_Reg ServiceRequester_m [] = {
	{ "getRequestName", ServiceRequester_getRequestName },
	{ "getRequesterNames", ServiceRequester_getRequesterNames },
	{ "ready", ServiceRequester_ready },
	{ "disconnect", ServiceRequester_disconnect },
	{ "requires", ServiceRequester_requires },
	{ NULL, NULL }
};


/***************************************************************
 * TaskContext (boxed)
 ***************************************************************/

gen_push_bxptr(TaskContext_push, "TaskContext", TaskContext)

static int TaskContext_getName(lua_State *L)
{
	const char *s;
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	s = tc->getName().c_str();
	lua_pushstring(L, s);
	return 1;
}

static int TaskContext_start(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	bool b = tc->start();
	lua_pushboolean(L, b);
	return 1;
}

static int TaskContext_stop(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	bool b = tc->stop();
	lua_pushboolean(L, b);
	return 1;
}

static int TaskContext_configure(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	bool ret = tc->configure();
	lua_pushboolean(L, ret);
	return 1;
}

static int TaskContext_activate(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	bool ret = tc->activate();
	lua_pushboolean(L, ret);
	return 1;
}

static int TaskContext_cleanup(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	bool ret = tc->cleanup();
	lua_pushboolean(L, ret);
	return 1;
}

static int TaskContext_getState(lua_State *L)
{
	TaskCore::TaskState ts;
	TaskContext **tc = (TaskContext**) luaM_checkudata_bx(L, 1, TaskContext);
	ts = (*tc)->getTaskState();

	switch(ts) {
	case TaskCore::Init: 		lua_pushstring(L, "Init"); break;
	case TaskCore::PreOperational:	lua_pushstring(L, "PreOperational"); break;
	case TaskCore::FatalError:	lua_pushstring(L, "FatalError"); break;
	case TaskCore::Exception:	lua_pushstring(L, "Exception"); break;
	case TaskCore::Stopped:		lua_pushstring(L, "Stopped"); break;
	case TaskCore::Running:		lua_pushstring(L, "Running"); break;
	case TaskCore::RunTimeError:	lua_pushstring(L, "RunTimeError"); break;
	default: 			lua_pushstring(L, "unknown");
	}
	return 1;
}

/* string-table getPeers(TaskContext self)*/
/* should better return array of TC's */
static int TaskContext_getPeers(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	std::vector<std::string> plist = tc->getPeerList();
	push_vect_str(L, plist);
	return 1;
}

/* bool addPeer(TaskContext self, TaskContext peer)*/
static int TaskContext_addPeer(lua_State *L)
{
	bool ret;
	TaskContext *self = *(luaM_checkudata_bx(L, 1, TaskContext));
	TaskContext *peer = *(luaM_checkudata_bx(L, 2, TaskContext));
	ret = self->addPeer(peer);
	lua_pushboolean(L, ret);
	return 1;
}

/* void removePeer(TaskContext self, string peer)*/
static int TaskContext_removePeer(lua_State *L)
{
	std::string peer;
	TaskContext *self = *(luaM_checkudata_bx(L, 1, TaskContext));
	peer = luaL_checkstring(L, 2);
	self->removePeer(peer);
	return 0;
}

/* TaskContext getPeer(string name) */
static int TaskContext_getPeer(lua_State *L)
{
	std::string strpeer;
	TaskContext *peer;
	TaskContext *self = *(luaM_checkudata_bx(L, 1, TaskContext));
	strpeer = luaL_checkstring(L, 2);
	peer = self->getPeer(strpeer);

	if(!peer) {
		luaL_error(L, "TaskContext.getPeer: no peer %s", strpeer.c_str());
		goto out;
	}

	TaskContext_push(L, peer);
 out:
	return 1;
}

static int TaskContext_getPortNames(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	std::vector<std::string> plist = tc->ports()->getPortNames();
	push_vect_str(L, plist);
	return 1;
}


#if 0
static int __tc_addport(lua_State *L, TaskContext *tc, int tcind,
		       PortInterface *pi, int piind)
{
	int len;
	_DBG("in: gettop=%d", lua_gettop(L));
	/* manage TC to which the port has been added
	 * PortOwners={p1={tc1, tc2, tc3}, p2={tc3}}
	 */
	lua_getfield(L, LUA_ENVIRONINDEX, "PortOwners");
	if (lua_isnil(L, -1)) {
		_DBG("creating new PortOwners");
		lua_pop(L, 1);
		lua_newtable(L);
	} else {
		_DBG("PortOwners exists... len=%d", lua_objlen(L, -1));
	}

	/* now we have the "PortOwners" table at the top either way */
	lua_pushvalue(L, piind);	/* make copy of Port userdata */
	lua_rawget(L, -2);		/* get table at PortOwners[piind] */

	if (lua_isnil(L, -1)) {
		_DBG("creating new PortOwners[0x%x]", (unsigned long) pi);
		lua_pop(L, 1);
		lua_newtable(L);	/* create new table for adding TC's */
	} else {
		_DBG("PortOwners[0x%x] exists, len=%d", (unsigned long) pi, lua_objlen(L, -1));
	}

	lua_pushvalue(L, 1); 	/* stack: -1 TC-userdata, -2 PortOwners[pi] table, -3 PortOwners tab */
	len = lua_objlen(L, -2);
	lua_rawseti(L, -2, len+1);	/* store TC ud in table at next free index */
					/* stack: -1 PortOwners[pi] table, -2 PortOwners tab */

	lua_pushvalue(L, piind); /* push Port userdata (key), stack: port-ud, tab, PortOwners */
	lua_rawset(L, -3);	/* PortOwners[port-ud] = { tc }, stack: PortOwners */

	lua_setfield(L, LUA_ENVIRONINDEX, "PortOwners");

	_DBG("out: gettop=%d", lua_gettop(L));
}
#endif

static int TaskContext_addPort(lua_State *L)
{
	const char* name, *desc;
	PortInterface **pi;
	int argc = lua_gettop(L);
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));

	pi = (PortInterface**) luaL_testudata(L, 2, "InputPort");
	if(pi) goto check_name;

	pi = (PortInterface**) luaL_testudata(L, 2, "OutputPort");
	if(pi) goto check_name;

	return luaL_error(L, "addPort: invalid argument, not a Port");

 check_name:
	if(argc > 2) {
		name = luaL_checkstring(L, 3);
		(*pi)->setName(name);
	}

	if(argc > 3) {
		desc = luaL_checkstring(L, 4);
		(*pi)->doc(desc);
	}

	tc->ports()->addPort(**pi);
 	return 0;
}

static int TaskContext_addEventPort(lua_State *L)
{
	const char* name, *desc;
	InputPortInterface **ipi;
	int argc = lua_gettop(L);
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));

	if((ipi = (InputPortInterface**) luaL_testudata(L, 2, "InputPort")) == NULL)
		return luaL_error(L, "addEventPort: invalid argument, not an InputPort");

	if(argc > 2) {
		name = luaL_checkstring(L, 3);
		(*ipi)->setName(name);
	}

	if(argc > 3) {
		desc = luaL_checkstring(L, 4);
		(*ipi)->doc(desc);
	}

	tc->ports()->addEventPort(**ipi);
 	return 0;
}

static int TaskContext_getPort(lua_State *L)
{
	const char* name;
	PortInterface *pi;
	InputPortInterface *ipi;
	OutputPortInterface *opi;

	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	name = luaL_checkstring(L, 2);

	pi = tc->getPort(name);
	if(!pi)
		luaL_error(L, "TaskContext.getPort: no port %s for taskcontext %s",
			   name, tc->getName().c_str());

	/* input or output? */
	if ((ipi = dynamic_cast<InputPortInterface *> (pi)) != NULL)
		InputPort_push(L, ipi);
	else if ((opi = dynamic_cast<OutputPortInterface *> (pi)) != NULL)
		OutputPort_push(L, opi);
	else
		luaL_error(L, "TaskContext.getPort: unknown port returned");

	return 1;
}

static int TaskContext_removePort(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	const char *port = luaL_checkstring(L, 2);
	tc->ports()->removePort(port);
	return 0;
}

static int TaskContext_addProperty(lua_State *L)
{
	const char *name, *desc;
	int argc = lua_gettop(L);
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	PropertyBase *pb = *(luaM_checkudata_mt_bx(L, 2, "Property", PropertyBase));

	if(argc > 2) {
		name = luaL_checkstring(L, 3);
		pb->setName(name);
	}

	if(argc > 3) {
		desc = luaL_checkstring(L, 4);
		pb->setDescription(desc);
	}


	if(!tc->addProperty(*pb))
		luaL_error(L, "TaskContext.addProperty: failed to add property %s.",
			   pb->getName().c_str());

	return 0;
}

static int TaskContext_getProperty(lua_State *L)
{
	const char *name;
	PropertyBase *prop;

	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	name = luaL_checkstring(L, 2);

	prop = tc->getProperty(name);

	if(!prop)
		luaL_error(L, "%s failed. No such property", __FILE__);

	Property_push(L, prop);
	return 1;
}

static int TaskContext_getProperties(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	vector<PropertyBase*> props = tc->properties()->getProperties();

	int key = 1;
	lua_createtable(L, props.size(), 0);
	for(vector<PropertyBase*>::iterator it = props.begin(); it != props.end(); ++it) {
		Property_push(L, *it);
		lua_rawseti(L, -2, key++);
	}

	return 1;
}

static int TaskContext_removeProperty(lua_State *L)
{
	const char *name;
	PropertyBase *prop;

	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	name = luaL_checkstring(L, 2);

	prop = tc->getProperty(name);

	if(!prop)
		luaL_error(L, "%s failed. No such property", __FILE__);

	tc->properties()->remove(prop);
	return 0;
}


static int TaskContext_getOps(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	std::vector<std::string> oplst = tc->operations()->getNames();
	push_vect_str(L, oplst);
	return 1;
}

/* returns restype, arity, table-of-arg-descr */
static int TaskContext_getOpInfo(lua_State *L)
{
	int i=1;
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	const char *op = luaL_checkstring(L, 2);
	std::vector<ArgumentDescription> args;

	if(!tc->operations()->hasMember(op))
		luaL_error(L, "TaskContext.getOpInfo failed: no such operation");

	lua_pushstring(L, tc->operations()->getResultType(op).c_str()); /* result type */
	lua_pushinteger(L, tc->operations()->getArity(op));		/* arity */
	lua_pushstring(L, tc->operations()->getDescription(op).c_str()); /* description */

	args = tc->operations()->getArgumentList(op);

	lua_newtable(L);

	for (std::vector<ArgumentDescription>::iterator it = args.begin(); it != args.end(); it++) {
		lua_newtable(L);
		lua_pushstring(L, "name"); lua_pushstring(L, it->name.c_str()); lua_rawset(L, -3);
		lua_pushstring(L, "type"); lua_pushstring(L, it->type.c_str()); lua_rawset(L, -3);
		lua_pushstring(L, "desc"); lua_pushstring(L, it->description.c_str()); lua_rawset(L, -3);
		lua_rawseti(L, -2, i++);
	}

	return 4;
}

static int TaskContext_provides(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	Service::shared_ptr srv = tc->provides();

	if(srv == 0)
		luaL_error(L, "TaskContext.provides: no default service");

	/* forward to Serivce.provides */
	luaM_pushobject_mt(L, "Service", Service::shared_ptr)(srv);
	lua_replace(L, 1);
	return Service_provides(L);
}

static int TaskContext_requires(lua_State *L)
{
	ServiceRequester *sr;
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	sr = tc->requires();

	if(!sr)
		luaL_error(L, "TaskContext.requires returned NULL");

	ServiceRequester_push(L, sr);
	lua_replace(L, 1);
	return ServiceRequester_requires(L);
}

static int TaskContext_connectServices(lua_State *L)
{
	int ret;
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	TaskContext *peer = *(luaM_checkudata_bx(L, 2, TaskContext));
	ret = tc->connectServices(peer);
	lua_pushboolean(L, ret);
	return 1;
}

static int __TaskContext_call(lua_State *L)
{
	unsigned int argc = lua_gettop(L);
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	const char *op = luaL_checkstring(L, 2);

	TaskContext *this_tc = __getTC(L);
	std::vector<base::DataSourceBase::shared_ptr> args;
	DataSourceBase::shared_ptr dsb;
	DataSourceBase::shared_ptr *dsbp;
	DataSourceBase::shared_ptr ret, ret2;
	types::TypeInfo *ti;

	OperationInterfacePart *orp = tc->operations()->getPart(op);

	if(!orp)
		luaL_error(L, "TaskContext.call: no operation %s", op);

	if(orp->arity() != argc-2)
		luaL_error(L, "TaskContext.call: wrong number of args. expected %d, got %d", orp->arity(), argc-2);

	for(unsigned int arg=3; arg<=argc; arg++) {
		/* fastpath: Variable argument */
		if ((dsbp = luaM_testudata_mt(L, arg, "Variable", DataSourceBase::shared_ptr)) != NULL) {
			dsb = *dsbp;
		} else  {
			/* slowpath: convert lua value to dsb */
			std::string type = orp->getArgumentType(arg-2)->getTypeName().c_str();
			dsb = Variable_fromlua(L, type.c_str(), arg);
		}
		args.push_back(dsb);
	}

	ret = tc->operations()->produce(op, args, this_tc->engine());

	/* not so nice: construct a ValueDataSource for the return Value
	 * todo: at least avoid the type conversion to string.
	 */
	if(orp->resultType() != "void") {
		ti = types::TypeInfoRepository::Instance()->type(orp->resultType());
		if(!ti)
			luaL_error(L, "TaskContext.call: failed to construct result type %s",
				   orp->resultType().c_str());
		ret2 = ti->buildValue();
		ret2->update(ret.get());
		Variable_push_coerce(L, ret2);
	} else {
		ret->evaluate();
		lua_pushnil(L);
	}
	return 1;
}

static int TaskContext_call(lua_State *L)
{
	int ret;
	try {
		ret = __TaskContext_call(L);
	} catch(...) {
		luaL_error(L, "TaskContext.call: caught exception");
	}
	return ret;
}


static int TaskContext_hasOperation(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	Service::shared_ptr srv = tc->provides();

	if(srv == 0)
		luaL_error(L, "TaskContext.provides: no default service");

	/* forward to Serivce.hasOperation */
	luaM_pushobject_mt(L, "Service", Service::shared_ptr)(srv);
	lua_replace(L, 1);
	return Service_hasOperation(L);
}


static int TaskContext_getOperation(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	Service::shared_ptr srv = tc->provides();

	if(srv == 0)
		luaL_error(L, "TaskContext.getOperation: no default service");

	/* forward to Serivce.getOperation */
	luaM_pushobject_mt(L, "Service", Service::shared_ptr)(srv);
	lua_replace(L, 1);
	return Service_getOperation(L);
}

/*
 * SendHandle (required for send)
 */

static void SendStatus_push(lua_State *L, SendStatus ss)
{
	switch (ss) {
	case SendSuccess:  lua_pushstring(L, "SendSuccess"); break;
	case SendNotReady: lua_pushstring(L, "SendNotReady"); break;
	case SendFailure:  lua_pushstring(L, "SendFailure"); break;
	default: 	   lua_pushstring(L, "unkown");
	}
}

static int __SendHandle_collect(lua_State *L, bool block)
{
	std::vector<DataSourceBase::shared_ptr> coll_args;
	SendStatus ss;
	const types::TypeInfo *ti;
	OperationInterfacePart *orp;
	DataSourceBase::shared_ptr tmpdsb;
	int coll_argc;

	SendHandleC *shc = luaM_checkudata_mt(L, 1, "SendHandle", SendHandleC);

	/* get orp pointer */
	orp = shc->getOrp();
	coll_argc = orp->collectArity();

	/* create appropriate datasources */
	for(int i=1; i <= coll_argc; i++) {
		ti = orp->getCollectType(i);
		tmpdsb = ti->buildValue();
		coll_args.push_back(tmpdsb);
		shc->arg(tmpdsb);
	}

	if(block) ss = shc->collect();
	else ss = shc->collectIfDone();

	SendStatus_push(L, ss);

	/* store all DSB in coll_args to luabind::object */
	for (int i=0; i<coll_argc; i++) {
		Variable_push_coerce(L, coll_args[i]);
	}
	/* SendStatus + collect args */
	return coll_argc + 1;
}

static int SendHandle_collect(lua_State *L)
{
	return __SendHandle_collect(L, true);
}

static int SendHandle_collectIfDone(lua_State *L)
{
	return __SendHandle_collect(L, false);
}


static const struct luaL_Reg SendHandle_f [] = {
	{ "collect", SendHandle_collect },
	{ "collectIfDone", SendHandle_collectIfDone },
	{ NULL, NULL }
};

static const struct luaL_Reg SendHandle_m [] = {
	{ "collect", SendHandle_collect },
	{ "collectIfDone", SendHandle_collectIfDone },
	{ "__gc", GCMethod<SendHandleC> },
	{ NULL, NULL }
};

/* TaskContext continued */
static int __TaskContext_send(lua_State *L)
{
	unsigned int argc = lua_gettop(L);
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	TaskContext *this_tc = __getTC(L);
	const char *op = luaL_checkstring(L, 2);

	OperationInterfacePart *orp = tc->operations()->getPart(op);
	OperationCallerC *occ = new OperationCallerC(orp, op, this_tc->engine()); // todo: alloc on stack?
	DataSourceBase::shared_ptr dsb;
	DataSourceBase::shared_ptr *dsbp;

	if(!orp)
		luaL_error(L, "TaskContext.send: no operation %s for TaskContext %s",
			   op, tc->getName().c_str());

	if(orp->arity() != argc-2)
		luaL_error(L, "TaskContext.send: wrong number of args. expected %d, got %d",
			   orp->arity(), argc);

	for(unsigned int arg=3; arg<=argc; arg++) {
		/* fastpath: Variable argument */
		if ((dsbp = luaM_testudata_mt(L, arg, "Variable", DataSourceBase::shared_ptr)) != NULL) {
			dsb = *dsbp;
		} else  {
			/* slowpath: convert lua value to dsb */
			std::string type = orp->getArgumentType(arg-2)->getTypeName().c_str();
			dsb = Variable_fromlua(L, type.c_str() ,arg);
		}
		occ->arg(dsb);
	}

	/* call send and construct push SendHandle userdata */
	luaM_pushobject_mt(L, "SendHandle", SendHandleC)(occ->send());
	return 1;
}

static int TaskContext_send(lua_State *L)
{
	int ret;
	try {
		ret = __TaskContext_send(L);
	} catch(...) {
		luaL_error(L, "TaskContext.send: caught exception");
	}
	return ret;
}

/* only explicit destruction allowed */
static int TaskContext_del(lua_State *L)
{
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	delete tc;

	/* this prevents calling rtt methods which would cause a crash */
	luaL_getmetatable(L, "__dead__");
	lua_setmetatable(L, -2);
	return 0;
}

#if 0
/* dispatcher: if a method with name then use that, else forward to
 * default service.
 *
 * This is useful but also potentially very confusing, because it
 * creates a mix of ':' and '.' for calling methods. For some like
 * 'activate' which are duplicates this is really bad. For consistency
 * it would be better to have sth. like TaskContext:ops() which
 * returns an indexable object that returns services like:
 * TC:ops().op_1("hullo")
 */
static int TaskContext_index(lua_State *L)
{
	Service::shared_ptr srv;
	TaskContext *tc = *(luaM_checkudata_bx(L, 1, TaskContext));
	const char* key = luaL_checkstring(L, 2);

	lua_getmetatable(L, 1);
	lua_getfield(L, -1, key);

	/* Either key is name of a method in the metatable */
	if(!lua_isnil(L, -1))
		return 1;

	/* ... or its a field access, so recall as self.get(self, value). */
	lua_settop(L, 2);
	srv = tc->provides();
	luaM_pushobject_mt(L, "Service", Service::shared_ptr)(srv);
	lua_replace(L, 1);
	/* todo: could fall back on ports? */
	return Service_getOperation(L);
}
#endif

static const struct luaL_Reg TaskContext_f [] = {
	{ "getName", TaskContext_getName },
	{ "start", TaskContext_start },
	{ "stop", TaskContext_stop },
	{ "configure", TaskContext_configure },
	{ "activate", TaskContext_activate },
	{ "cleanup", TaskContext_cleanup },
	{ "getState", TaskContext_getState },
	{ "getPeers", TaskContext_getPeers },
	{ "addPeer", TaskContext_addPeer },
	{ "removePeer", TaskContext_removePeer },
	{ "getPeer", TaskContext_getPeer },
	{ "getPortNames", TaskContext_getPortNames },
	{ "addPort", TaskContext_addPort },
	{ "addEventPort", TaskContext_addEventPort },
	{ "getPort", TaskContext_getPort },
	{ "removePort", TaskContext_removePort },
	{ "addProperty", TaskContext_addProperty },
	{ "getProperty", TaskContext_getProperty },
	{ "getProperties", TaskContext_getProperties },
	{ "removeProperty", TaskContext_removeProperty },
	{ "getOps", TaskContext_getOps },
	{ "getOpInfo", TaskContext_getOpInfo },
	{ "hasOperation", TaskContext_hasOperation },
	{ "provides", TaskContext_provides },
	{ "connectServices", TaskContext_connectServices },
	{ "call", TaskContext_call },
	{ "getOperation", TaskContext_getOperation },
	{ "send", TaskContext_send },
	{ "delete", TaskContext_del },
	{ NULL, NULL}
};

static const struct luaL_Reg TaskContext_m [] = {
	{ "getName", TaskContext_getName },
	{ "start", TaskContext_start },
	{ "stop", TaskContext_stop },
	{ "configure", TaskContext_configure },
	{ "activate", TaskContext_activate },
	{ "cleanup", TaskContext_cleanup },
	{ "getState", TaskContext_getState },
	{ "getPeers", TaskContext_getPeers },
	{ "addPeer", TaskContext_addPeer },
	{ "removePeer", TaskContext_removePeer },
	{ "getPeer", TaskContext_getPeer },
	{ "getPortNames", TaskContext_getPortNames },
	{ "addPort", TaskContext_addPort },
	{ "addEventPort", TaskContext_addEventPort },
	{ "getPort", TaskContext_getPort },
	{ "removePort", TaskContext_removePort },
	{ "addProperty", TaskContext_addProperty },
	{ "getProperty", TaskContext_getProperty },
	{ "getProperties", TaskContext_getProperties },
	{ "removeProperty", TaskContext_removeProperty },
	{ "getOps", TaskContext_getOps },
	{ "getOpInfo", TaskContext_getOpInfo },
	{ "hasOperation", TaskContext_hasOperation },
	{ "provides", TaskContext_provides },
	{ "requires", TaskContext_requires },
	{ "connectServices", TaskContext_connectServices },
	{ "call", TaskContext_call },
	{ "getOperation", TaskContext_getOperation },
	{ "send", TaskContext_send },
	{ "delete", TaskContext_del },
	// { "__index", TaskContext_index },
	/* we don't GC TaskContexts
	 * { "__gc", GCMethod<TaskContext> }, */
	{ NULL, NULL}
};

/*
 * Execution engine hook registration
 */

/* executable IF */
class EEHook : public base::ExecutableInterface
{
protected:
	std::string func;
	lua_State *L;
	TaskContext *tc; /* remember this to be able to print TC name
			    in error messages */
public:
	EEHook(lua_State *_L, std::string _func) { L = _L; func = _func; tc = __getTC(L); }
	bool execute() { return call_func(L, func.c_str(), tc); }
};

static int EEHook_new(lua_State *L)
{
	const char *func;
	func = luaL_checkstring(L, 1);
	luaM_pushobject(L, EEHook)(L, func);
	return 1;
}

static int EEHook_enable(lua_State *L)
{
	EEHook *eeh = luaM_checkudata(L, 1, EEHook);
	TaskContext *tc = __getTC(L);
	lua_pushboolean(L, tc->engine()->runFunction(eeh));
	return 1;
}

static int EEHook_disable(lua_State *L)
{	EEHook *eeh = luaM_checkudata(L, 1, EEHook);
	TaskContext *tc = __getTC(L);
	lua_pushboolean(L, tc->engine()->removeFunction(eeh));
	return 1;
}

static int EEHook_gc(lua_State *L)
{
	EEHook_disable(L);
	lua_settop(L, 1);
	reinterpret_cast<EEHook*>(lua_touserdata(L, 1))->~EEHook();
	return 0;
}

static const struct luaL_Reg EEHook_f [] = {
	{ "new", EEHook_new },
	{ "enable", EEHook_enable },
	{ "disable", EEHook_disable },
};


static const struct luaL_Reg EEHook_m [] = {
	{ "enable", EEHook_enable },
	{ "disable", EEHook_disable },
	{ "__gc", EEHook_gc },
};


/*
 * Logger and miscellaneous
 */
static const char *const loglevels[] = {
	"Never", "Fatal", "Critical", "Error", "Warning", "Info", "Debug", "RealTime", NULL
};

static int Logger_setLogLevel(lua_State *L)
{
	Logger::LogLevel ll = (Logger::LogLevel) luaL_checkoption(L, 1, NULL, loglevels);
	log().setLogLevel(ll);
	return 0;
}

static int Logger_getLogLevel(lua_State *L)
{
	Logger::LogLevel ll = log().getLogLevel();

	switch(ll) {
	case Logger::Never:	lua_pushstring(L, "Never"); break;
	case Logger::Fatal:	lua_pushstring(L, "Fatal"); break;
	case Logger::Critical:	lua_pushstring(L, "Critical"); break;
	case Logger::Error:	lua_pushstring(L, "Error"); break;
	case Logger::Warning:	lua_pushstring(L, "Warning"); break;
	case Logger::Info: 	lua_pushstring(L, "Info"); break;
	case Logger::Debug:	lua_pushstring(L, "Debug"); break;
	case Logger::RealTime:	lua_pushstring(L, "RealTime"); break;
	default:
		lua_pushstring(L, "unknown");
	}
	return 1;
}

static int Logger_log(lua_State *L)
{
	const char *mes;
	for(int i=1; i<=lua_gettop(L); i++) {
		mes = luaL_checkstring(L, i);
		Logger::log() << mes;
	}
	Logger::log() << endlog();
	return 0;
}

static int Logger_logl(lua_State *L)
{
	const char *mes;
	Logger::LogLevel ll = (Logger::LogLevel) luaL_checkoption(L, 1, NULL, loglevels);
	for(int i=2; i<=lua_gettop(L); i++) {
		mes = luaL_checkstring(L, i);
		Logger::log(ll) << mes;
	}
	Logger::log(ll) << endlog();
	return 0;
}

/* misc stuff */

static int getTC(lua_State *L)
{
	lua_pushstring(L, "this_TC");
	lua_gettable(L, LUA_REGISTRYINDEX);
	return 1;
}

static TaskContext* __getTC(lua_State *L)
{
	TaskContext *tc;
	getTC(L);
	tc = *(luaM_checkudata_bx(L, -1, TaskContext));
	lua_pop(L, 1);
	return tc;
}

static int rtt_services(lua_State *L)
{
	push_vect_str(L, PluginLoader::Instance()->listServices());
	return 1;
}

static int rtt_typekits(lua_State *L)
{
	push_vect_str(L, PluginLoader::Instance()->listTypekits());
	return 1;
}

static int rtt_types(lua_State *L)
{
	push_vect_str(L, TypeInfoRepository::Instance()->getTypes());
	return 1;
}

static const struct luaL_Reg rtt_f [] = {
	{"getTC", getTC },
	{"services", rtt_services },
	{"typekits", rtt_typekits },
	{"types", rtt_types },
	{"setLogLevel", Logger_setLogLevel },
	{"getLogLevel", Logger_getLogLevel },
	{"log", Logger_log },
	{"logl", Logger_logl },
	{NULL, NULL}
};

extern "C" int luaopen_rtt(lua_State *L);

int luaopen_rtt(lua_State *L)
{
	lua_newtable(L);
	lua_replace(L, LUA_ENVIRONINDEX);

	luaL_newmetatable(L, "__dead__");

	/* register MyObj
	 * 1. line creates metatable MyObj and registers name in registry
	 * 2. line duplicates metatable
	 * 3. line sets metatable[__index]=metatable
	 *    (more precisely: table at -2 [__index] = top_of_stack, pops top of stack)
	 * 4. line register methods in metatable
	 * 5. line registers free functions in global mystuff.MyObj table
	 */
	luaL_newmetatable(L, "TaskContext");
	lua_pushvalue(L, -1); /* duplicates metatable */
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, TaskContext_m);
	luaL_register(L, "rtt.TaskContext", TaskContext_f);

	luaL_newmetatable(L, "Operation");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, Operation_m);
	luaL_register(L, "rtt.Operation", Operation_f);

	luaL_newmetatable(L, "Service");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, Service_m);
	luaL_register(L, "rtt.Service", Service_f);

	luaL_newmetatable(L, "ServiceRequester");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, ServiceRequester_m);
	luaL_register(L, "rtt.ServiceRequester", ServiceRequester_f);

	luaL_newmetatable(L, "SendHandle");
	lua_pushvalue(L, -1); /* duplicates metatable */
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, SendHandle_m);
	luaL_register(L, "rtt.SendHandle", SendHandle_f);

	luaL_newmetatable(L, "InputPort");
	lua_pushvalue(L, -1); /* duplicates metatable */
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, InputPort_m);
	luaL_register(L, "rtt.InputPort", InputPort_f);

	luaL_newmetatable(L, "OutputPort");
	lua_pushvalue(L, -1); /* duplicates metatable */
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, OutputPort_m);
	luaL_register(L, "rtt.OutputPort", OutputPort_f);

	luaL_newmetatable(L, "Variable");
	lua_pushvalue(L, -1); /* duplicates metatable */
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, Variable_m);
	luaL_register(L, "rtt.Variable", Variable_f);

	luaL_newmetatable(L, "Property");
	lua_pushvalue(L, -1); /* duplicates metatable */
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, Property_m);
	luaL_register(L, "rtt.Property", Property_f);

	luaL_newmetatable(L, "EEHook");
	lua_pushvalue(L, -1); /* duplicates metatable */
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, EEHook_m);
	luaL_register(L, "rtt.EEHook", EEHook_f);

	/* misc toplevel functions */
	luaL_register(L, "rtt", rtt_f);

	return 1;
}

/* store the TC to be returned by getTC() in registry */
int set_context_tc(TaskContext *tc, lua_State *L)
{
	TaskContext **new_tc;
	lua_pushstring(L, "this_TC");
	new_tc = (TaskContext**) lua_newuserdata(L, sizeof(TaskContext*));
	*new_tc = (TaskContext*) tc;
	luaL_getmetatable(L, "TaskContext");
	lua_setmetatable(L, -2);
	lua_settable(L, LUA_REGISTRYINDEX);
	return 0;
}


/* call a zero arity function with a boolean return value
 * used to call various hooks */
bool call_func(lua_State *L, const std::string &fname, TaskContext *tc)
{
	bool ret;

	lua_getglobal(L, fname.c_str());

	if (lua_pcall(L, 0, 1, NULL) != 0) {
		Logger::log(Logger::Error) << "LuaComponent '"<< tc->getName()  <<"': error calling function "
					   << fname << ": " << lua_tostring(L, -1) << endlog();
		ret = false;
		goto out;
	}
	if (!lua_isboolean(L, -1)) {
		Logger::log(Logger::Error) << "LuaComponent '" << tc->getName() << "': " << fname
					   << " must return a bool but returned a"
					   << lua_typename(L, lua_type(L, -1)) << endlog();
		ret = false;
		goto out;
	}

	ret = lua_toboolean(L, -1);

 out:
	return ret;
}
