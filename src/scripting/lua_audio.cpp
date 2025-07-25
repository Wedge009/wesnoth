/*
	Copyright (C) 2017 - 2025
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#include "lua_audio.hpp"

#include "log.hpp"
#include "preferences/preferences.hpp"
#include "resources.hpp"
#include "scripting/lua_common.hpp"
#include "scripting/push_check.hpp"
#include "sound.hpp"
#include "sound_music_track.hpp"
#include "soundsource.hpp"
#include <set>
#include <utility>

static lg::log_domain log_audio("audio");
#define DBG_AUDIO LOG_STREAM(debug, log_audio)
#define LOG_AUDIO LOG_STREAM(info, log_audio)
#define ERR_AUDIO LOG_STREAM(err, log_audio)

static const char* Track = "music track";
static const char* Source = "sound source";

class lua_music_track {
	std::shared_ptr<sound::music_track> track;
public:
	explicit lua_music_track(std::shared_ptr<sound::music_track>&& new_track) : track(std::move(new_track)) {}
	sound::music_track& operator*() {
		return *track;
	}
	const sound::music_track& operator*() const {
		return *track;
	}
	std::shared_ptr<sound::music_track> operator->() {
		return track;
	}
	std::shared_ptr<const sound::music_track> operator->() const {
		return track;
	}
};

static lua_music_track* push_track(lua_State* L, std::shared_ptr<sound::music_track>&& new_track) {
	if(new_track) {
		lua_music_track* trk = new(L) lua_music_track(std::move(new_track));
		luaL_setmetatable(L, Track);
		return trk;
	} else {
		lua_pushnil(L);
		return nullptr;
	}
}

static lua_music_track* get_track(lua_State* L, int i) {
	return static_cast<lua_music_track*>(luaL_checkudata(L, i, Track));
}

class lua_sound_source {
	soundsource::sourcespec spec;
public:
	explicit lua_sound_source(const soundsource::sourcespec& spec) : spec(spec) {}
	lua_sound_source(lua_sound_source&) = delete;
	soundsource::sourcespec& operator*() {
		return spec;
	}
	const soundsource::sourcespec& operator*() const {
		return spec;
	}
	soundsource::sourcespec* operator->() {
		return &spec;
	}
	const soundsource::sourcespec* operator->() const {
		return &spec;
	}
};

static lua_sound_source& push_source(lua_State* L, const soundsource::sourcespec& spec) {
	lua_sound_source* src = new(L) lua_sound_source(spec);
	luaL_setmetatable(L, Source);
	return *src;
}

static lua_sound_source& get_source(lua_State* L, int i) {
	return *static_cast<lua_sound_source*>(luaL_checkudata(L, i, Source));
}

/**
 * Destroys a lua_music_track object before it is collected (__gc metamethod).
 */
static int impl_track_collect(lua_State* L)
{
	lua_music_track* u = get_track(L, 1);
	u->lua_music_track::~lua_music_track();
	return 0;
}

static int impl_music_get(lua_State* L) {
	if(lua_isnumber(L, 2)) {
		push_track(L, sound::get_track(lua_tointeger(L, 2) - 1));
		return 1;
	}
	const char* m = luaL_checkstring(L, 2);

	if(strcmp(m, "current") == 0) {
		push_track(L, sound::get_current_track());
		return 1;
	}

	if(strcmp(m, "previous") == 0) {
		push_track(L, sound::get_previous_music_track());
		return 1;
	}

	if(strcmp(m, "current_i") == 0) {
		auto current_index = sound::get_current_track_index();
		if(current_index) {
			lua_pushinteger(L, *current_index + 1);
		} else {
			lua_pushnil(L);
		}
		return 1;
	}
	if(strcmp(m, "all") == 0) {
		config playlist;
		sound::write_music_play_list(playlist);
		const auto& range = playlist.child_range("music");
		std::vector<config> tracks(range.begin(), range.end());
		lua_push(L, tracks);
		return 1;
	}
	// This calculation reverses the one used in [volume] to get back the relative volume level.
	// (Which is the same calculation that's duplicated in impl_music_set.)
	return_float_attrib("volume", sound::get_music_volume() * 100.0 / prefs::get().music_volume());
	return luaW_getmetafield(L, 1, m);
}

static int impl_music_set(lua_State* L) {
	if(lua_isnumber(L, 2)) {
		unsigned int i = lua_tointeger(L, 2) - 1;
		config cfg;
		if(lua_isnil(L, 3)) {
			if(i < sound::get_num_tracks()) {
				sound::remove_track(i);
			}
		} else if(luaW_toconfig(L, 3, cfg)) {
			// Don't clear the playlist
			cfg["append"] = true;
			// Don't allow play_once=yes
			if(cfg["play_once"].to_bool()) {
				return luaL_argerror(L, 3, "For play_once, use wesnoth.music_list.play instead");
			}
			if(i >= sound::get_num_tracks()) {
				sound::play_music_config(cfg);
			} else {
				// Remove the track at that index and add the new one in its place
				// It's a little inefficient though...
				sound::remove_track(i);
				sound::play_music_config(cfg, false, i);
			}
		} else {
			lua_music_track& track = *get_track(L, 3);
			if(i < sound::get_num_tracks()) {
				sound::set_track(i, track.operator->());
			} else {
				track->write(cfg, true);
				sound::play_music_config(cfg);
			}
		}
		return 0;
	}
	const char* m = luaL_checkstring(L, 2);
	modify_float_attrib_check_range("volume", sound::set_music_volume(value * prefs::get().music_volume() / 100.0), 0.0, 100.0);
	modify_int_attrib_check_range("current_i", sound::play_track(value - 1), 1, static_cast<int>(sound::get_num_tracks()));
	return 0;
}

static int impl_music_len(lua_State* L) {
	lua_pushinteger(L, sound::get_num_tracks());
	return 1;
}

static int intf_music_play(lua_State* L) {
	sound::play_music_once(luaL_checkstring(L, 1));
	return 0;
}

static int intf_music_next(lua_State*) {
	std::size_t n = sound::get_num_tracks();
	if(n > 0) {
		sound::play_track(n);
	}
	return 0;
}

static int intf_music_add(lua_State* L) {
	int index = -1;
	if(lua_isinteger(L, 1)) {
		index = lua_tointeger(L, 1);
		lua_remove(L, 1);
	}
	config cfg = config {
		"name", luaL_checkstring(L, 1),
		"append", true,
	};
	bool found_ms_before = false, found_ms_after = false, found_imm = false;
	for(int i = 2; i <= lua_gettop(L); i++) {
		if(lua_isboolean(L, i)) {
			if(found_imm) {
				return luaL_argerror(L, i, "only one boolean argument may be passed");
			} else {
				cfg["immediate"] = luaW_toboolean(L, i);
			}
		} else if(lua_isnumber(L, i)) {
			if(found_ms_after) {
				return luaL_argerror(L, i, "only two integer arguments may be passed");
			} else if(found_ms_before) {
				cfg["ms_after"] = lua_tointeger(L, i);
				found_ms_after = true;
			} else {
				cfg["ms_before"] = lua_tointeger(L, i);
				found_ms_before = true;
			}
		} else {
			return luaL_argerror(L, i, "unrecognized argument");
		}
	}
	sound::play_music_config(cfg, false, index);
	return 0;
}

static int intf_music_clear(lua_State*) {
	sound::empty_playlist();
	return 0;
}

static int intf_music_remove(lua_State* L) {
	// Use a non-standard comparator to ensure iteration in descending order
	std::set<int, std::greater<int>> to_remove;
	for(int i = 1; i <= lua_gettop(L); i++) {
		to_remove.insert(luaL_checkinteger(L, i));
	}
	for(int i : to_remove) {
		sound::remove_track(i);
	}
	return 0;
}

static int intf_music_commit(lua_State*) {
	sound::commit_music_changes();
	return 0;
}

static int impl_track_get(lua_State* L) {
	lua_music_track* track = get_track(L, 1);
	if(track == nullptr) {
		return luaL_error(L, "Error: Attempted to access an invalid music track.\n");
	}
	const char* m = luaL_checkstring(L, 2);
	return_bool_attrib("append", (*track)->append());
	return_bool_attrib("shuffle", (*track)->shuffle());
	return_bool_attrib("immediate", (*track)->immediate());
	return_bool_attrib("once", (*track)->play_once());
	return_int_attrib("ms_before", (*track)->ms_before().count());
	return_int_attrib("ms_after", (*track)->ms_after().count());
	return_string_attrib("name", (*track)->id());
	return_string_attrib("title", (*track)->title());

	return_cfg_attrib("__cfg",
						cfg["append"]=(*track)->append();
						cfg["shuffle"]=(*track)->shuffle();
						cfg["immediate"]=(*track)->immediate();
						cfg["once"]=(*track)->play_once();
						cfg["ms_before"]=(*track)->ms_before();
						cfg["ms_after"]=(*track)->ms_after();
						cfg["name"]=(*track)->id();
						cfg["title"]=(*track)->title());

	return luaW_getmetafield(L, 1, m);
}

static int impl_track_set(lua_State* L) {
	lua_music_track* track = get_track(L, 1);
	if(track == nullptr) {
		return luaL_error(L, "Error: Attempted to access an invalid music track.\n");
	}
	const char* m = luaL_checkstring(L, 2);
	modify_bool_attrib("shuffle", (*track)->set_shuffle(value));
	modify_bool_attrib("once", (*track)->set_play_once(value));
	modify_int_attrib("ms_before", (*track)->set_ms_before(std::chrono::milliseconds{value}));
	modify_int_attrib("ms_after", (*track)->set_ms_after(std::chrono::milliseconds{value}));
	modify_string_attrib("title", (*track)->set_title(value));
	return 0;
}

static int impl_track_eq(lua_State* L) {
	lua_music_track* a = get_track(L, 1);
	lua_music_track* b = get_track(L, 2);
	if(!a || !b) {
		// This implies that one argument is not a music track, though I suspect this is dead code...?
		// Does Lua ever call this if the arguments are not of the same type?
		lua_pushboolean(L, false);
		return 1;
	}
	lua_music_track& lhs = *a;
	lua_music_track& rhs = *b;
	lua_pushboolean(L, lhs->id() == rhs->id() && lhs->shuffle() == rhs->shuffle() && lhs->play_once() == rhs->play_once() && lhs->ms_before() == rhs->ms_before() && lhs->ms_after() == rhs->ms_after());
	return 1;
}

/**
 * Get an existing sound source
 * Key: The sound source ID
 */
static int impl_sndsrc_get(lua_State* L) {
	if(!resources::soundsources) {
		return 0;
	}
	std::string id = luaL_checkstring(L, 2);
	if(!resources::soundsources->contains(id)) {
		return 0;
	}
	push_source(L, resources::soundsources->get(id));
	return 1;
}

/**
 * Adds or removes a sound source by its ID
 * Key: sound source ID
 * Value: Table containing keyword arguments, existing sound source userdata, or nil to delete
 */
static int impl_sndsrc_set(lua_State* L) {
	if(!resources::soundsources) {
		return 0;
	}
	std::string id = luaL_checkstring(L, 2);
	config cfg;
	if(lua_isnil(L, 3)) {
		resources::soundsources->remove(id);
	} else if(luaW_toconfig(L, 3, cfg)) {
		cfg["id"] = id;
		soundsource::sourcespec spec(cfg);
		resources::soundsources->add(spec);
		resources::soundsources->update();
	} else {
		auto& src = get_source(L, 3);
		resources::soundsources->add(*src);
		resources::soundsources->update();
	}
	return 0;
}
static int impl_source_collect(lua_State* L)
{
	lua_sound_source& u = get_source(L, 1);
	u.lua_sound_source::~lua_sound_source();
	return 0;
}

static int impl_source_get(lua_State* L) {
	lua_sound_source& src = get_source(L, 1);
	const char* m = luaL_checkstring(L, 2);
	return_string_attrib("id", src->id());
	return_vector_string_attrib("sounds", utils::split(src->files()));
	return_int_attrib("delay", src->minimum_delay().count());
	return_int_attrib("chance", src->chance());
	return_int_attrib("loop", src->loops());
	return_int_attrib("range", src->full_range());
	return_int_attrib("fade_range", src->fade_range());
	return_bool_attrib("check_fogged", src->check_fogged());
	return_bool_attrib("check_shrouded", src->check_shrouded());
	return_cfg_attrib("__cfg", src->write(cfg));

	if(strcmp(m, "locations") == 0) {
		const auto& locs = src->get_locations();
		lua_createtable(L, locs.size(), 0);
		for(const auto& loc : locs) {
			luaW_pushlocation(L, loc);
			lua_rawseti(L, -1, lua_rawlen(L, -2) + 1);
		}
	}

	return luaW_getmetafield(L, 1, m);
}

static int impl_source_set(lua_State* L) {
	lua_sound_source& src = get_source(L, 1);
	const char* m = luaL_checkstring(L, 2);
	modify_int_attrib("delay", src->set_minimum_delay(std::chrono::milliseconds{value}));
	modify_int_attrib("chance", src->set_chance(value));
	modify_int_attrib("loop", src->set_loops(value));
	modify_int_attrib("range", src->set_full_range(value));
	modify_int_attrib("fade_range", src->set_fade_range(value));
	modify_bool_attrib("check_fogged", src->set_check_fogged(value));
	modify_bool_attrib("check_shrouded", src->set_check_shrouded(value));

	if(strcmp(m, "sounds") == 0) {
		std::string files;
		if(lua_istable(L, 3)) {
			files = utils::join(lua_check<std::vector<std::string>>(L, 3));
		} else {
			files = luaL_checkstring(L, 3);
		}
		src->set_files(files);
	}

	if(strcmp(m, "locations") == 0) {
		std::vector<map_location> locs;
		locs.resize(1);
		if(luaW_tolocation(L, 3, locs[0])) {

		} else {
			locs.clear();
			for(lua_pushnil(L); lua_next(L, 3); lua_pop(L, 1)) {
				locs.push_back(luaW_checklocation(L, -1));
			}
		}
		src->set_locations(locs);
	}

	// Now apply the change
	resources::soundsources->add(*src);
	resources::soundsources->update();
	return 0;
}

static int impl_source_eq(lua_State* L) {
	lua_sound_source& a = get_source(L, 1);
	lua_sound_source& b = get_source(L, 2);
	if(a->id() != b->id()) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_pushboolean(L,
		a->files() == b->files() &&
		a->minimum_delay() == b->minimum_delay() &&
		a->chance() == b->chance() &&
		a->loops() == b->loops() &&
		a->full_range() == b->full_range() &&
		a->fade_range() == b->fade_range() &&
		a->check_fogged() == b->check_fogged() &&
		a->check_shrouded() == b->check_shrouded() &&
		std::set<map_location>(a->get_locations().begin(), a->get_locations().end()) == std::set<map_location>(b->get_locations().begin(), b->get_locations().end())
	);
	return 1;
}

/**
 * Gets the current sound volume
 * - Return: Current volume
 */
static int impl_audio_get(lua_State* L)
{
	std::string m = luaL_checkstring(L, 2);
	if(m != "volume") return 0;
	int vol = prefs::get().sound_volume();
	lua_pushnumber(L, sound::get_sound_volume() * 100.0 / vol);
	return 1;
}

/**
 * Sets the current sound volume
 * - Arg: New volume to set
 */
static int impl_audio_set(lua_State* L)
{
	std::string m = luaL_checkstring(L, 2);
	if(m != "volume") {
		lua_rawset(L, 1);
		return 0;
	}
	int vol = prefs::get().sound_volume();
	float rel = lua_tonumber(L, 3);
	if(rel < 0.0f || rel > 100.0f) {
		return luaL_argerror(L, 1, "volume must be in range 0..100");
	}
	vol = static_cast<int>(rel*vol / 100.0f);
	sound::set_sound_volume(vol);
	return 0;
}

namespace lua_audio {
	std::string register_table(lua_State* L) {
		// Metatable to enable the volume attribute
		luaW_getglobal(L, "wesnoth", "audio");
		lua_createtable(L, 0, 2);
		static luaL_Reg vol_callbacks[] {
			{ "__index", impl_audio_get },
			{ "__newindex", impl_audio_set },
			{ nullptr, nullptr },
		};
		luaL_setfuncs(L, vol_callbacks, 0);
		lua_setmetatable(L, -2);

		// The music playlist metatable
		lua_newuserdatauv(L, 0, 0);
		lua_createtable(L, 0, 10);
		static luaL_Reg pl_callbacks[] {
			{ "__index", impl_music_get },
			{ "__newindex", impl_music_set },
			{ "__len", impl_music_len },
			{ "play", intf_music_play },
			{ "add", intf_music_add },
			{ "clear", intf_music_clear },
			{ "remove", intf_music_remove },
			{ "next", intf_music_next },
			{ "force_refresh", intf_music_commit },
			{ nullptr, nullptr },
		};
		luaL_setfuncs(L, pl_callbacks, 0);
		lua_pushstring(L, "music playlist");
		lua_setfield(L, -2, "__metatable");
		lua_setmetatable(L, -2);
		lua_setfield(L, -2, "music_list");

		// The sound source map metatable
		lua_newuserdatauv(L, 0, 0);
		lua_createtable(L, 0, 3);
		static luaL_Reg slm_callbacks[] {
			{ "__index", impl_sndsrc_get },
			{ "__newindex", impl_sndsrc_set },
			{ nullptr, nullptr },
		};
		luaL_setfuncs(L, slm_callbacks, 0);
		lua_pushstring(L, "sound source map");
		lua_setfield(L, -2, "__metatable");
		lua_setmetatable(L, -2);
		lua_setfield(L, -2, "sources");
		lua_pop(L, 1);

		// The music track metatable
		luaL_newmetatable(L, Track);
		static luaL_Reg track_callbacks[] {
			{"__gc", impl_track_collect},
			{ "__index", impl_track_get },
			{ "__newindex", impl_track_set },
			{ "__eq", impl_track_eq },
			{ nullptr, nullptr },
		};
		luaL_setfuncs(L, track_callbacks, 0);
		lua_pushstring(L, Track);
		lua_setfield(L, -2, "__metatable");
		lua_pop(L, 1);

		// The sound source metatable
		luaL_newmetatable(L, Source);
		static luaL_Reg source_callbacks[] {
			{"__gc", impl_source_collect},
			{ "__index", impl_source_get },
			{ "__newindex", impl_source_set },
			{ "__eq", impl_source_eq },
			{ nullptr, nullptr },
		};
		luaL_setfuncs(L, source_callbacks, 0);
		lua_pushstring(L, Source);
		lua_setfield(L, -2, "__metatable");

		return "Adding music playlist table...\n";
	}
}
