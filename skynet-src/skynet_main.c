#include "skynet.h"

#include "skynet_imp.h"
#include "skynet_env.h"
#include "skynet_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>
#include <assert.h>

/**
  * @brief 尝试获取key对应的val，如果未定义
  *    则将其设置为opt对应的植并返�
  * @param[in] key 关键字
  * @param[in] opt 值
  */
static int
optint(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		char tmp[20];
		sprintf(tmp,"%d",opt);
		skynet_setenv(key, tmp);
		return opt;
	}
	return strtol(str, NULL, 10);
}

/*
static int
optboolean(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		skynet_setenv(key, opt ? "true" : "false");
		return opt;
	}
	return strcmp(str,"true")==0;
}
*/

/**
  * @brief 尝试获取key对应的val，如果未定义
  *    则将其设置为opt对应的植并返�
  * @param[in] key 关键字
  * @param[in] opt 值
  */
static const char *
optstring(const char *key,const char * opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		if (opt) {
			skynet_setenv(key, opt);
			opt = skynet_getenv(key);
		}
		return opt;
	}
	return str;
}

static void
_init_env(lua_State *L) {
	lua_pushnil(L);  /* first key */
	while (lua_next(L, -2) != 0) {
		int keyt = lua_type(L, -2);
		if (keyt != LUA_TSTRING) {
			fprintf(stderr, "Invalid config table\n");
			exit(1);
		}
		const char * key = lua_tostring(L,-2);
		if (lua_type(L,-1) == LUA_TBOOLEAN) {
			int b = lua_toboolean(L,-1);
			skynet_setenv(key,b ? "true" : "false" );
		} else {
			const char * value = lua_tostring(L,-1);
			if (value == NULL) {
				fprintf(stderr, "Invalid config table key = %s\n", key);
				exit(1);
			}
			skynet_setenv(key,value);
		}
		lua_pop(L,1);
	}
	lua_pop(L,1);
}

/**
  * @brief 用于屏蔽管道破裂
  */
int sigign() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, 0);
	return 0;
}

static const char * load_config = "\
	local config_name = ...\
	local f = assert(io.open(config_name))\
	local code = assert(f:read \'*a\')\
	local function getenv(name) return assert(os.getenv(name), \'os.getenv() failed: \' .. name) end\
	code = string.gsub(code, \'%$([%w_%d]+)\', getenv)\
	f:close()\
	local result = {}\
	assert(load(code,\'=(load)\',\'t\',result))()\
	return result\
";

int
main(int argc, char *argv[]) {
	const char * config_file = NULL ;
	if (argc > 1) {
		/*从参数获取配置文件路径*/
		config_file = argv[1];
	} else {
		fprintf(stderr, "Need a config file. Please read skynet wiki : https://github.com/cloudwu/skynet/wiki/Config\n"
			"usage: skynet configfilename\n");
		return 1;
	}

	/*初始化线程共享参数*/
	skynet_globalinit();
	/*加载lua环境*/
	skynet_env_init();
	/*屏蔽管道破裂*/
	sigign();
      /*加载lua模块*/
	struct skynet_config config;

      /*初始lua交互结构*/
	struct lua_State *L = lua_newstate(skynet_lalloc, NULL);
	 /*加载指定lua库*/
	luaL_openlibs(L);	// link lua lib

	/*加载配置文件*/
	int err = luaL_loadstring(L, load_config);
	assert(err == LUA_OK);
	
	lua_pushstring(L, config_file);

	err = lua_pcall(L, 1, 1, 0);
	if (err) {
		fprintf(stderr,"%s\n",lua_tostring(L,-1));
		lua_close(L);
		return 1;
	}
	_init_env(L);

	config.thread =  optint("thread",8);
	/*尝试获取thread的值，默认设置为 8*/
	config.module_path = optstring("cpath","./cservice/?.so");
	/*尝试获取harbor的值，默认设置为 1*/
	config.harbor = optint("harbor", 1);
	/*尝试获取bootstrap的值，默认设置为 bootstrap*/
	config.bootstrap = optstring("bootstrap","snlua bootstrap");
	/*尝试获取daemon的值*/
	config.daemon = optstring("daemon", NULL);
	/*尝试获取logger的值*/
	config.logger = optstring("logger", NULL);

	lua_close(L);

	/*开始运行skynet*/  
	skynet_start(&config);

	/*清理工作*/
	skynet_globalexit();

	return 0;
}
