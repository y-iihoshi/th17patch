
#include <vector>
#include <string>

#include <boost/filesystem/path.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/utility.hpp>

#include <Windows.h>
#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include "main.h"
#include "dat_utility.h"

namespace DatUtility {

namespace {

#pragma warning(push)
#pragma warning(disable: 4512)
struct FindListExt {
	FindListExt(const char *ext) : ext(ext) { }
	const char * const ext;
	bool operator()(boost::shared_ptr<LIST> file) const {
		const char * const file_ext = ::PathFindExtensionA(file->fn);
		return (::strcmp(ext, file_ext) == 0);
	}
};
#pragma warning(pop)

} // anonymous

unsigned int CalcKeyIndex(boost::shared_ptr<const LIST> file_data) {
	BOOST_ASSERT(file_data);
  return CalcKeyIndex(file_data->fn, ::strlen(file_data->fn));
}

unsigned int CalcKeyIndex(const char * const name, const unsigned int length) {
	unsigned int result = 0;
	for(unsigned int i = 0; i < length; i++) {
		result += static_cast<unsigned char>(name[i]);
	}
	return (result & 7);
}

bool FindFile(const std::vector<boost::shared_ptr<LIST> > &file_list, const char * const name, boost::shared_ptr<LIST> &result) {
  for (const boost::shared_ptr<LIST> file : file_list) {
    if (::strncmp(file->fn, name, sizeof(file->fn)) == 0) {
      result = file;
      return true;
    }
  }
  return false;
}

} // DatUtility
