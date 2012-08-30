/*
   This file is part of darktable,
   copyright (c) 2012 Jeremy Rosen

   darktable is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   darktable is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with darktable.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lua/image.h"
#include "lua/stmt.h"
#include "common/darktable.h"
#include "common/debug.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/colorlabels.h"
#include "common/history.h"
#include "common/metadata.h"
#include "metadata_gen.h"

/***********************************************************************
  handling of dt_image_t
 **********************************************************************/
static const char * image_typename = "dt_lua_image";
typedef struct {
	int imgid;
} lua_image;


static const dt_image_t*dt_lua_checkreadimage(lua_State*L,int index) {
	lua_image* tmp_image=luaL_checkudata(L,index,image_typename);
	return dt_image_cache_read_get(darktable.image_cache,tmp_image->imgid);
}

static void dt_lua_releasereadimage(lua_State*L,const dt_image_t* image) {
	dt_image_cache_read_release(darktable.image_cache,image);
}

static dt_image_t*dt_lua_checkwriteimage(lua_State*L,int index) {
	const dt_image_t* my_readimage=dt_lua_checkreadimage(L,index);
	return dt_image_cache_write_get(darktable.image_cache,my_readimage);
}

static void dt_lua_releasewriteimage(lua_State*L,dt_image_t* image) {
	dt_image_cache_write_release(darktable.image_cache,image,DT_IMAGE_CACHE_SAFE);
	dt_lua_releasereadimage(L,image);
}


void dt_lua_image_push(lua_State * L,int imgid) {
	if(dt_lua_singleton_find(L,imgid,image_typename)) {
		return;
	}
	// check that id is valid
	sqlite3_stmt *stmt;
	DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select id from images where id = ?1", -1, &stmt, NULL);
	DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, imgid);
	if(sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		luaL_error(L,"invalid id for image : %d",imgid);
		return; // lua_error never returns, this is here to remind us of the fact
	}
	sqlite3_finalize(stmt);
	lua_image * my_image = (lua_image*)lua_newuserdata(L,sizeof(lua_image));
	my_image->imgid=imgid;
	dt_lua_singleton_register(L,imgid,image_typename);
}


static int image_clone(lua_State *L) {
	lua_image* tmp_image=luaL_checkudata(L,-1,image_typename);
	const dt_image_t *my_image= dt_image_cache_read_get(darktable.image_cache,tmp_image->imgid);
	dt_lua_image_push(L,dt_image_duplicate(my_image->id));
	return 1;
}

typedef enum {
	EXIF_EXPOSURE,
	EXIF_APERTURE,
	EXIF_ISO,
	EXIF_FOCAL_LENGTH,
	EXIF_FOCUS_DISTANCE,
	EXIF_CROP,
	EXIF_MAKER,
	EXIF_MODEL,
	EXIF_LENS,
	EXIF_DATETIME_TAKEN,
	FILENAME,
	PATH,
	DUP_INDEX,
	WIDTH,
	HEIGHT,
	IS_LDR,
	IS_HDR,
	IS_RAW,
	RATING,
	ID,
	COLORLABEL,
	CREATOR,
	PUBLISHER,
	TITLE,
	DESCRIPTION,
	RIGHTS,
	HISTORY,
	DUPLICATE,
	LAST_IMAGE_FIELD
} image_fields;
const char *image_fields_name[] = {
	"exif_exposure",
	"exif_aperture",
	"exif_iso",
	"exif_focal_length",
	"exif_focus_distance",
	"exif_crop",
	"exif_maker",
	"exif_model",
	"exif_lens",
	"exif_datetime_taken",
	"filename",
	"path",
	"duplicate_index",
	"width",
	"height",
	"is_ldr",
	"is_hdr",
	"is_raw",
	"rating",
	"id",
	"colorlabels",
	"creator",
	"publisher",
	"title",
	"description",
	"rights",
	"history",
	"duplicate",
	NULL
};

static int image_index(lua_State *L){
	const dt_image_t * my_image=dt_lua_checkreadimage(L,-2);
	switch(luaL_checkoption(L,-1,NULL,image_fields_name)) {
		case EXIF_EXPOSURE:
			lua_pushnumber(L,my_image->exif_exposure);
			break;
		case EXIF_APERTURE:
			lua_pushnumber(L,my_image->exif_aperture);
			break;
		case EXIF_ISO:
			lua_pushnumber(L,my_image->exif_iso);
			break;
		case EXIF_FOCAL_LENGTH:
			lua_pushnumber(L,my_image->exif_focal_length);
			break;
		case EXIF_FOCUS_DISTANCE:
			lua_pushnumber(L,my_image->exif_focus_distance);
			break;
		case EXIF_CROP:
			lua_pushnumber(L,my_image->exif_crop);
			break;
		case EXIF_MAKER:
			lua_pushstring(L,my_image->exif_maker);
			break;
		case EXIF_MODEL:
			lua_pushstring(L,my_image->exif_model);
			break;
		case EXIF_LENS:
			lua_pushstring(L,my_image->exif_lens);
			break;
		case EXIF_DATETIME_TAKEN:
			lua_pushstring(L,my_image->exif_datetime_taken);
			break;
		case FILENAME:
			lua_pushstring(L,my_image->filename);
			break;
		case PATH:
			{
				sqlite3_stmt *stmt;
				DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
						"select folder from images, film_rolls where "
						"images.film_id = film_rolls.id and images.id = ?1", -1, &stmt, NULL);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, my_image->id);
				if(sqlite3_step(stmt) == SQLITE_ROW)
				{
					lua_pushstring(L,(char *)sqlite3_column_text(stmt, 0));
				} else {
					sqlite3_finalize(stmt);
					dt_lua_releasereadimage(L,my_image);
					return luaL_error(L,"should never happen");
				}
				sqlite3_finalize(stmt);
				break;
			}
		case DUP_INDEX:
			{
				// get duplicate suffix
				int version = 0;
				sqlite3_stmt *stmt;
				DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
						"select count(id) from images where filename in "
						"(select filename from images where id = ?1) and film_id in "
						"(select film_id from images where id = ?1) and id < ?1",
						-1, &stmt, NULL);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, my_image->id);
				if(sqlite3_step(stmt) == SQLITE_ROW)
					version = sqlite3_column_int(stmt, 0);
				sqlite3_finalize(stmt);
				lua_pushinteger(L,version);
				break;
			}
		case WIDTH:
			lua_pushinteger(L,my_image->width);
			break;
		case HEIGHT:
			lua_pushinteger(L,my_image->height);
			break;
		case IS_LDR:
			lua_pushboolean(L,dt_image_is_ldr(my_image));
			break;
		case IS_HDR:
			lua_pushboolean(L,dt_image_is_hdr(my_image));
			break;
		case IS_RAW:
			lua_pushboolean(L,dt_image_is_raw(my_image));
			break;
		case RATING:
			{
				int score = my_image->flags & 0x7;
				if(score >6) score=5;
				if(score ==6) score=-1;

				lua_pushinteger(L,score);
				break;
			}
		case ID:
			lua_pushinteger(L,my_image->height);
			break;
		case COLORLABEL:
			{
				dt_colorlabels_lua_push(L,my_image->id);
				break;
			}
		case CREATOR:
			{
				sqlite3_stmt *stmt;
				DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),"select value from meta_data where id = ?1 and key = ?2", -1, &stmt, NULL);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, my_image->id);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, DT_METADATA_XMP_DC_CREATOR);
				if(sqlite3_step(stmt) != SQLITE_ROW) {
					lua_pushstring(L,"");
				} else {
					lua_pushstring(L,(char *)sqlite3_column_text(stmt, 0));
				}
				sqlite3_finalize(stmt);
				break;

			}
		case PUBLISHER:
			{
				sqlite3_stmt *stmt;
				DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),"select value from meta_data where id = ?1 and key = ?2", -1, &stmt, NULL);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, my_image->id);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, DT_METADATA_XMP_DC_PUBLISHER);
				if(sqlite3_step(stmt) != SQLITE_ROW) {
					lua_pushstring(L,"");
				} else {
					lua_pushstring(L,(char *)sqlite3_column_text(stmt, 0));
				}
				sqlite3_finalize(stmt);
				break;

			}
		case TITLE:
			{
				sqlite3_stmt *stmt;
				DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),"select value from meta_data where id = ?1 and key = ?2", -1, &stmt, NULL);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, my_image->id);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, DT_METADATA_XMP_DC_TITLE);
				if(sqlite3_step(stmt) != SQLITE_ROW) {
					lua_pushstring(L,"");
				} else {
					lua_pushstring(L,(char *)sqlite3_column_text(stmt, 0));
				}
				sqlite3_finalize(stmt);
				break;

			}
		case DESCRIPTION:
			{
				sqlite3_stmt *stmt;
				DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),"select value from meta_data where id = ?1 and key = ?2", -1, &stmt, NULL);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, my_image->id);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, DT_METADATA_XMP_DC_DESCRIPTION);
				if(sqlite3_step(stmt) != SQLITE_ROW) {
					lua_pushstring(L,"");
				} else {
					lua_pushstring(L,(char *)sqlite3_column_text(stmt, 0));
				}
				sqlite3_finalize(stmt);
				break;

			}
		case RIGHTS:
			{
				sqlite3_stmt *stmt;
				DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),"select value from meta_data where id = ?1 and key = ?2", -1, &stmt, NULL);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 1, my_image->id);
				DT_DEBUG_SQLITE3_BIND_INT(stmt, 2, DT_METADATA_XMP_DC_RIGHTS);
				if(sqlite3_step(stmt) != SQLITE_ROW) {
					lua_pushstring(L,"");
				} else {
					lua_pushstring(L,(char *)sqlite3_column_text(stmt, 0));
				}
				sqlite3_finalize(stmt);
				break;

			}
		case HISTORY:
			{
				dt_history_lua_push(L,my_image->id);
				break;
			}
		case DUPLICATE:
			lua_pushcfunction(L,image_clone); // works as a lua member, i.e meant to be invoked with :
			break;

		default:
			dt_lua_releasereadimage(L,my_image);
			return luaL_error(L,"should never happen %s",lua_tostring(L,-1));

	}
	dt_lua_releasereadimage(L,my_image);
	return 1;
}

static int image_newindex(lua_State *L){
	dt_image_t * my_image=dt_lua_checkwriteimage(L,-3);
	switch(luaL_checkoption(L,-2,NULL,image_fields_name)) {
		case EXIF_EXPOSURE:
			my_image->exif_exposure = luaL_checknumber(L,-1);
			break;
		case EXIF_APERTURE:
			my_image->exif_aperture = luaL_checknumber(L,-1);
			break;
		case EXIF_ISO:
			my_image->exif_iso = luaL_checknumber(L,-1);
			break;
		case EXIF_FOCAL_LENGTH:
			my_image->exif_focal_length = luaL_checknumber(L,-1);
			break;
		case EXIF_FOCUS_DISTANCE:
			my_image->exif_focus_distance = luaL_checknumber(L,-1);
			break;
		case EXIF_CROP:
			my_image->exif_crop = luaL_checknumber(L,-1);
			break;
		case EXIF_MAKER:
			{
				size_t tgt_size;
				const char * value = luaL_checklstring(L,-1,&tgt_size);
				if(tgt_size > 32) {
					dt_lua_releasewriteimage(L,my_image);
					return luaL_error(L,"string value too long");
				}
				strncpy(my_image->exif_maker,value,32);
				break;
			}
		case EXIF_MODEL:
			{
				size_t tgt_size;
				const char * value = luaL_checklstring(L,-1,&tgt_size);
				if(tgt_size > 32) {
					dt_lua_releasewriteimage(L,my_image);
					return luaL_error(L,"string value too long");
				}
				strncpy(my_image->exif_maker,value,32);
				break;
			}
		case EXIF_LENS:
			{
				size_t tgt_size;
				const char * value = luaL_checklstring(L,-1,&tgt_size);
				if(tgt_size > 32) {
					dt_lua_releasewriteimage(L,my_image);
					return luaL_error(L,"string value too long");
				}
				strncpy(my_image->exif_model,value,32);
				break;
			}
		case EXIF_DATETIME_TAKEN:
			{
				size_t tgt_size;
				const char * value = luaL_checklstring(L,-1,&tgt_size);
				if(tgt_size > 52) {
					dt_lua_releasewriteimage(L,my_image);
					return luaL_error(L,"string value too long");
				}
				strncpy(my_image->exif_lens,value,52);
				break;
			}
		case RATING:
			{
				int my_score = luaL_checkinteger(L,-1);
				if(my_score > 5) {
					dt_lua_releasewriteimage(L,my_image);
					return luaL_error(L,"rating too high : %d",my_score);
				}
				if(my_score == -1) my_score = 6;
				if(my_score < -1) {
					dt_lua_releasewriteimage(L,my_image);
					return luaL_error(L,"rating too low : %d",my_score);
				}
				my_image->flags &= ~0x7;
				my_image->flags |= my_score;
				break;
			}

		case CREATOR:
			dt_metadata_set(my_image->id,"Xmp.dc.creator",luaL_checkstring(L,-1));
			dt_image_synch_xmp(my_image->id);
			break;
		case PUBLISHER:
			dt_metadata_set(my_image->id,"Xmp.dc.publisher",luaL_checkstring(L,-1));
			dt_image_synch_xmp(my_image->id);
			break;
		case TITLE:
			dt_metadata_set(my_image->id,"Xmp.dc.title",luaL_checkstring(L,-1));
			dt_image_synch_xmp(my_image->id);
			break;
		case DESCRIPTION:
			dt_metadata_set(my_image->id,"Xmp.dc.description",luaL_checkstring(L,-1));
			dt_image_synch_xmp(my_image->id);
			break;
		case RIGHTS:
			dt_metadata_set(my_image->id,"Xmp.dc.title",luaL_checkstring(L,-1));
			dt_image_synch_xmp(my_image->id);
			break;
		case HISTORY:
			{
				if(lua_isnil(L,-1)) {
					dt_history_delete_on_image(my_image->id);
					break;
				}
				int source_id = dt_history_lua_check(L,-1);
				dt_history_copy_and_paste_on_image(source_id, my_image->id, 0);
				break;
			}
		case FILENAME:
		case PATH:
		case DUP_INDEX:
		case WIDTH:
		case HEIGHT:
		case IS_LDR:
		case IS_HDR:
		case IS_RAW:
		case ID:
		case COLORLABEL:
		case DUPLICATE:
			dt_lua_releasewriteimage(L,my_image);
			return luaL_error(L,"read only field : ",lua_tostring(L,-2));
		default:
			dt_lua_releasewriteimage(L,my_image);
			return luaL_error(L,"unknown index for image : ",lua_tostring(L,-2));

	}
	dt_lua_releasewriteimage(L,my_image);
	return 0;
}

static int image_tostring(lua_State *L) {
	const dt_image_t * my_image=dt_lua_checkreadimage(L,-1);
	char image_name[PATH_MAX];
	dt_image_full_path(my_image->id,image_name,PATH_MAX);
	dt_image_path_append_version(my_image->id,image_name,PATH_MAX);
	lua_pushstring(L,image_name);
	dt_lua_releasereadimage(L,my_image);
	return 1;
}
static const luaL_Reg image_meta[] = {
	{"__index", image_index },
	{"__newindex", image_newindex },
	{"__tostring", image_tostring },
	{0,0}
};
static int image_init(lua_State * L) {
	luaL_newmetatable(L,image_typename);
	luaL_setfuncs(L,image_meta,0);
	dt_lua_init_name_list_pair(L, image_fields_name);
	dt_lua_init_singleton(L);
	return 0;
}



dt_lua_type dt_lua_image = {
	image_init
};

/***********************************************************************
  Creating the images global variable
 **********************************************************************/
static int images_next(lua_State *L) {
	//printf("%s\n",__FUNCTION__);
	//TBSL : check index and find the correct position in stmt if index was changed manually
	// 2 args, table, index, returns the next index,value or nil,nil return the first index,value if index is nil 

	sqlite3_stmt *stmt = dt_lua_stmt_check(L,-2);
	if(lua_isnil(L,-1)) {
		sqlite3_reset(stmt);
	} else if(luaL_checkinteger(L,-1) != sqlite3_column_int(stmt,0)) {
		return luaL_error(L,"TBSL : changing index of a loop on variable images is not supported yet");
	}
	int result = sqlite3_step(stmt);
	if(result != SQLITE_ROW){
		return 0;
	}
	int imgid = sqlite3_column_int(stmt,0);
	lua_pushinteger(L,imgid);
	dt_lua_image_push(L,imgid);
	return 2;
}

static int images_index(lua_State *L) {
	int imgid = luaL_checkinteger(L,-1);
	dt_lua_image_push(L,imgid);
	return 1;
}

static int images_pairs(lua_State *L) {
	lua_pushcfunction(L,images_next);
	sqlite3_stmt *stmt;
	DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select id from images", -1, &stmt, NULL);
	dt_lua_stmt_push(L,stmt);
	lua_pushnil(L); // index set to null for reset
	return 3;
}
static int images_len(lua_State *L) {
	sqlite3_stmt *stmt;
	DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select count(id) from images", -1, &stmt, NULL);
	if(sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return luaL_error(L,"unknown error while searching the number of images");
	}
	lua_pushinteger(L, sqlite3_column_int(stmt,0));
	return 1;
}

static const luaL_Reg images_meta[] = {
	{"__pairs", images_pairs },
	{"__index", images_index },
	{"__len", images_len },
	{0,0}
};

static int images_init(lua_State * L) {
	lua_newtable(L);
	luaL_setfuncs(L,images_meta,0);
	lua_newuserdata(L,1); // placeholder we can't use a table because we can't prevent assignment
	lua_pushvalue(L,-2);
	lua_setmetatable(L,-2);
	dt_lua_push_darktable_lib(L);
	lua_pushvalue(L,-2);
	lua_setfield(L,-2,"images");
	return 0;
}

dt_lua_type dt_lua_images = {
	images_init,
};

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
