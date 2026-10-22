#pragma once

#if defined(_WIN32) && defined(PLUGIN_BUILD)
#define PLUGIN_API __declspec(dllexport)
#elif defined(_WIN32)
#define PLUGIN_API __declspec(dllimport)
#else
#define PLUGIN_API
#endif

PLUGIN_API int plugin_answer();
