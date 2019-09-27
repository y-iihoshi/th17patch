
#include "noncopyable.h"

typedef struct data_list {
   char fn[100];
   unsigned int size;
   unsigned int comp_size;
   unsigned int addr;
} LIST;

class ExtractorBase : noncopyable<ExtractorBase> {
public:
   virtual unsigned int GetSize() const = 0;
};
