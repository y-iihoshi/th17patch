
typedef struct data_list {
   char fn[100];
   unsigned int size;
   unsigned int comp_size;
   unsigned int addr;
} LIST;

class ExtractorBase : boost::noncopyable {
public:
   virtual bool Extract(const unsigned int index, std::vector<unsigned char>& result) = 0;
   virtual unsigned int GetSize() const = 0;
   virtual std::wstring GetName() const = 0;
   virtual boost::filesystem::path GetFileName(unsigned int index) const = 0;
};
