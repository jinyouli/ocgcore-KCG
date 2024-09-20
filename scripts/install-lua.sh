#!/usr/bin/env bash

set -euxo pipefail

LUA_VERSION=5.3.6
LUA_ARCHIVE=tmp/lua-$LUA_VERSION.tar.gz
#ensure the script is always running inside the core's root directory
cd "$(dirname "$0")/.."

mkdir -p tmp
rm -rf lua/src

if [ ! -f $LUA_ARCHIVE ]; then
	curl --retry 2 --connect-timeout 30 --location https://github.com/lua/lua/archive/refs/tags/v$LUA_VERSION.tar.gz -o $LUA_ARCHIVE
fi

mkdir -p lua/src
tar xf $LUA_ARCHIVE --strip-components=1 -C lua/src
cd lua
cat <<EOT >> src/luaconf.h

#ifdef LUA_USE_WINDOWS
#undef LUA_USE_WINDOWS
#undef LUA_DL_DLL
#undef LUA_USE_C89
#endif

EOT
if [[ "${LUA_APICHECK:-0}" == "1" ]]; then
	cat <<EOT >> src/luaconf.h

#ifndef luaconf_h_ocgcore_api_check
#define luaconf_h_ocgcore_api_check
void ocgcore_lua_api_check(void* L, const char* error_message);
#define luai_apicheck(l,e)	do { if(!e) ocgcore_lua_api_check(l,"Lua api check failed: " #e); } while(0)


#endif
EOT
fi
