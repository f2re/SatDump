#define SATDUMP_DLL_EXPORT 1
#include "satdump_vars.h"

#include <cstdlib>
#include <filesystem>

#if defined (__APPLE__)
#include <mach-o/dyld.h>
#include <libgen.h>
#define LIBRARIES_SEARCH_PATH "/../Resources/"
#define RESOURCES_SEARCH_PATH "/../Resources/"
#elif defined (_WIN32)
#include <Windows.h>
#include <shlwapi.h>
#define LIBRARIES_SEARCH_PATH "\\..\\lib\\satdump\\"
#define RESOURCES_SEARCH_PATH "\\..\\share\\satdump\\"
#endif

namespace satdump
{
        static std::string normalize_directory(std::string path)
        {
            if (path.empty())
                return path;

            std::filesystem::path filesystem_path(path);
            path = filesystem_path.lexically_normal().generic_string();
            if (path.empty() || path.back() != '/')
                path += '/';
            return path;
        }

        static std::string environment_directory(const char *name)
        {
            const char *value = std::getenv(name);
            if (value == nullptr || value[0] == '\0')
                return "";
            return normalize_directory(value);
        }

#if defined (__APPLE__)
        std::string get_search_path(const char *target)
        {
            uint32_t bufsize = PATH_MAX;
            char exec_path[bufsize], ret_val[bufsize], search_dir[bufsize];
            char *exec_dir;
            _NSGetExecutablePath(exec_path, &bufsize);
            exec_dir = dirname(exec_path);
            strcpy(search_dir, exec_dir);
            strcat(search_dir, target);
            realpath(search_dir, ret_val);
            return normalize_directory(ret_val);
        }
#elif defined (_WIN32)
        std::string get_search_path(const char *target)
        {
            char exe_path[MAX_PATH], ret_val[MAX_PATH];
            GetModuleFileNameA(NULL, exe_path, MAX_PATH);
            PathRemoveFileSpecA(exe_path);
            strcat_s(exe_path, MAX_PATH, target);
            PathCanonicalizeA(ret_val, exe_path);
            return normalize_directory(ret_val);
        }
#endif

        std::string init_res_path()
        {
            const std::string environment_path = environment_directory("SATDUMP_RESOURCES_PATH");
            if (!environment_path.empty())
                return environment_path;

#if defined (__APPLE__) || defined (_WIN32)
            const std::string search_dir = get_search_path(RESOURCES_SEARCH_PATH);
            if (std::filesystem::exists(search_dir) && std::filesystem::is_directory(search_dir))
                return search_dir;
#ifdef _WIN32
            return get_search_path("\\");
#else
            return normalize_directory(RESOURCES_PATH);
#endif
#elif defined (__ANDROID__)
            return "./";
#else
            return normalize_directory(RESOURCES_PATH);
#endif
        }

        std::string init_lib_path()
        {
            const std::string environment_path = environment_directory("SATDUMP_LIBRARIES_PATH");
            if (!environment_path.empty())
                return environment_path;

#if defined (__APPLE__) || defined (_WIN32)
            const std::string search_dir = get_search_path(LIBRARIES_SEARCH_PATH);
            if (std::filesystem::exists(search_dir) && std::filesystem::is_directory(search_dir))
                return search_dir;
#ifdef _WIN32
            return get_search_path("\\");
#else
            return normalize_directory(LIBRARIES_PATH);
#endif
#elif defined (__ANDROID__)
            return "./";
#else
            return normalize_directory(LIBRARIES_PATH);
#endif
        }

        std::string RESPATH = init_res_path();
        std::string LIBPATH = init_lib_path();
}
