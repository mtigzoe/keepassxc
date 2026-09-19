# MINIZIP_FOUND                   - Minizip library was found
# MINIZIP_INCLUDE_DIR             - Path to Minizip include dir
# MINIZIP_LIBRARIES               - List of Minizip libraries

# vcpkg's debug import library is named minizipd(.lib), distinct from the
# release minizip(.lib), so a plain find_library() would always resolve to
# the release library/DLL even for a Debug build. Use SelectLibraryConfigurations
# to pick the correct one per-config, like vcpkg's own recommended pattern.
find_library(MINIZIP_LIBRARY_RELEASE NAMES minizip libminizip)
find_library(MINIZIP_LIBRARY_DEBUG NAMES minizipd libminizipd)
find_path(MINIZIP_INCLUDE_DIR zip.h PATH_SUFFIXES minizip)

include(SelectLibraryConfigurations)
select_library_configurations(MINIZIP)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Minizip DEFAULT_MSG MINIZIP_LIBRARIES MINIZIP_INCLUDE_DIR)
