
#include "stdafx.h"

// プロジェクト内のどれか一つのファイルに REAPERAPI_IMPLEMENT を定義してから
// reaper_plugin_functions.h を読み込むと、plugin_register()など一部の実体が
// 生成される(これがないとLinkErrorでハマることになる)
#define REAPERAPI_IMPLEMENT
#include "reaper_plugin_functions.h"
