
#include <cstdio>

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <filesystem>

#include <Windows.h>

#include "noncopyable.h"
#include "main.h"
#include "dat_utility.h"
#include "th_crypt.h"
#include "th_base.h"

/* デバッグ用
#include <boost/filesystem.hpp>
#include <boost/utility.hpp>
#include <org/click3/dll_hack_lib.h>
*/

class Th17 : public ThOwnerBase<Th17, 0x1B, 0x37, 0x075BCD15, 0x3ADE68B1, 0x08180754, 0x3E, 0x9B, 0x80> {
public:

   Th17(std::istream& in, const unsigned long long int file_size, const std::shared_ptr<const Header> header, const std::shared_ptr<const std::vector<FileRecord> > file_list) :
      ThOwnerBase(in, file_size, header, file_list)
   {
   }

   const unsigned int* GetConvMap(const unsigned int index) {
      static const unsigned int conv_map[8][4] = {
       {0x1B,	0x73,	0x0100,	0x3800},
       {0x12,	0x43,	0x0200,	0x3E00},
         {0x35,	0x79,	0x0400,	0x3C00},
         {0x03,	0x91,	0x0080,	0x6400},
         {0xab,	0xDC,	0x0080,	0x7000},
         {0x51,	0x9E,	0x0100,	0x4000},
         {0xC1,	0x15,	0x0400,	0x2C00},
         {0x99,	0x7D,	0x0080,	0x4400}
      };
      return conv_map[index];
   }

   const unsigned int* GetKey(const FileRecord &record) {
      if (record.name.empty()) {
         return nullptr;
      }
      return GetConvMap(DatUtility::CalcKeyIndex(&record.name.front(), record.name.length()));
   }
};


unsigned int GetPageSize(void) {
   SYSTEM_INFO info;
   ::GetSystemInfo(&info);
   return info.dwPageSize;
}

void* GetBaseAddr(const void* addr) {
   MEMORY_BASIC_INFORMATION mbi;
   ::VirtualQuery(addr, &mbi, sizeof(mbi));
   return mbi.BaseAddress;
}

bool ChangeProtect(unsigned int* old_protect, const void* addr, unsigned int new_protect) {
   void* const base_addr = GetBaseAddr(addr);
   const unsigned int page_size = GetPageSize();
   if (0 == ::VirtualProtect(base_addr, page_size, new_protect, reinterpret_cast<DWORD*>(old_protect))) {
      return false;
   }
   return true;
}

bool ChangeCode(unsigned int addr, const unsigned char* old_code, const unsigned char* new_code, unsigned int size) {
   unsigned char* write_ptr = reinterpret_cast<unsigned char*>(addr);
   unsigned int old_protect;
   if (!ChangeProtect(&old_protect, write_ptr, PAGE_EXECUTE_READWRITE)) {
      return false;
   }
   if (::memcmp(write_ptr, old_code, size) != 0) {
      ChangeProtect(&old_protect, write_ptr, old_protect);
      return false;
   }
   ::memcpy(write_ptr, new_code, size);
   unsigned int protect;
   if (!ChangeProtect(&protect, write_ptr, old_protect) || protect != PAGE_EXECUTE_READWRITE) {
      return false;
   }
   return true;
}


HANDLE th17dat = 0;
std::shared_ptr<std::ifstream> ifs;
std::shared_ptr<Th17> th17;
const FileRecord *prevRecord = nullptr;

HANDLE _stdcall d_CreateFileA(
   LPCSTR lpFileName,
   DWORD dwDesiredAccess,
   DWORD dwShareMode,
   LPSECURITY_ATTRIBUTES lpSecurityAttributes,
   DWORD dwCreationDisposition,
   DWORD dwFlagsAndAttributes,
   HANDLE hTemplateFile)
{
   const HANDLE result = ::CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
   if (result == INVALID_HANDLE_VALUE) {
      return INVALID_HANDLE_VALUE;
   }
   const std::filesystem::path path = lpFileName;
   if (path.filename() == "th17.dat") {
      th17dat = result;
      ifs = std::make_shared<std::ifstream>(path, std::ios::binary);
      th17 = Th17::Open(*ifs, std::filesystem::file_size(path));
   }
   return result;
}

const FileRecord& findRecord(const unsigned int addr, const unsigned int compressSize) {
   for (const FileRecord& item : *th17->file_list) {
      if (item.addr == addr) {
         if (item.compress_size != compressSize) {
            ::printf("Invalid: ");
         }
         return item;
      }
   }
   static const FileRecord dummy;
   return dummy;
}

BOOL _stdcall d_ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
   const unsigned int current = ::SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);
   const BOOL result = ::ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
   if (result == 0) {
      return 0;
   }
   if (hFile == th17dat) {
      const FileRecord &record = findRecord(current, *lpNumberOfBytesRead);
      prevRecord = &record;
      //::printf("%s\n", record.name.c_str());
   }
   return result;
}

bool ChangeIAT(HMODULE module) {
   static const struct {
      const std::string dllName;
      const std::string procName;
      const unsigned int procAddress;
   } targetList[] = {
     { "KERNEL32.dll", "CreateFileA", reinterpret_cast<unsigned int>(d_CreateFileA) },
     { "KERNEL32.dll", "ReadFile", reinterpret_cast<unsigned int>(d_ReadFile) },
   };
   unsigned char* const baseAddr = reinterpret_cast<unsigned char*>(module == NULL ? ::GetModuleHandleW(NULL) : module);
   const IMAGE_DOS_HEADER& mz = *reinterpret_cast<const IMAGE_DOS_HEADER*>(baseAddr);
   const IMAGE_NT_HEADERS32& pe = *reinterpret_cast<const IMAGE_NT_HEADERS32*>(baseAddr + mz.e_lfanew);
   const IMAGE_IMPORT_DESCRIPTOR* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(baseAddr + pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
   for (; desc->OriginalFirstThunk != 0; desc = ++desc) {
      const char* const dllName = reinterpret_cast<const char*>(baseAddr + desc->Name);
      const unsigned int* in = reinterpret_cast<unsigned int*>(baseAddr + desc->OriginalFirstThunk);
      unsigned int* out = reinterpret_cast<unsigned int*>(baseAddr + desc->FirstThunk);
      for (; *in != 0; ++in, ++out) {
         if ((*in & 0x80000000) != 0) {
            continue;
         }
         const char* const procName = reinterpret_cast<const char*>(baseAddr + *in + 2);
         for (const auto& target : targetList) {
            if (::_stricmp(target.dllName.c_str(), dllName) != 0 || target.procName != procName) {
               continue;
            }
            unsigned int oldProtect;
            ChangeProtect(&oldProtect, out, PAGE_READWRITE);
            *out = target.procAddress;
            ChangeProtect(&oldProtect, out, oldProtect);
         }
      }
   }
   return true;
}

void dumpSht(unsigned char * const data) {
   float& unknown = *reinterpret_cast<float*>(&data[0x04]);
   float& unknown2 = *reinterpret_cast<float*>(&data[0x08]);
   float& unknown3 = *reinterpret_cast<float*>(&data[0x0C]);
   ::printf("????: %f %f %f\n", unknown, unknown2, unknown3);
   float& speed = *reinterpret_cast<float*>(&data[0x10]);
   float& speed2 = *reinterpret_cast<float*>(&data[0x14]);
   float& speed3 = *reinterpret_cast<float*>(&data[0x18]);
   float& speed4 = *reinterpret_cast<float*>(&data[0x1C]);
   ::printf("速度: %f %f %f %f\n", speed, speed2, speed3, speed4);
   unsigned int& powerMax = *reinterpret_cast<unsigned int*>(&data[0x20]); // signed?
   ::printf("Power上限: %d\n", powerMax);
   unsigned int& unknown4 = *reinterpret_cast<unsigned int*>(&data[0x24]);
   ::printf("????: %d\n", unknown4);
   signed int& damageLimit = *reinterpret_cast<signed int*>(&data[0x28]);
   ::printf("ダメージキャップ: %d\n", damageLimit);
   float* const unknown5 = reinterpret_cast<float*>(&data[0x40]);// float[40];
   ::printf("????:\n");
   for (unsigned int i = 0; i < 40; i++) {
      ::printf("%5.2f ", unknown5[i]);
      unknown5[i] = -1.0;
      if (i % 4 == 3) {
         ::printf("\n");
      }
   }
}

// カワウソ妖夢1面の誤字「早詰み」を「早積み」に
void patchHayatsumi(unsigned char* const data) {
   if (prevRecord->name != "st01h.msg") {
      return;
   }
   if (data[1336] != 0x60 || data[1337] != 0xDE) {
      return;
   }
   data[1336] = 0x7B;
   data[1337] = 0x7D;
}

// 実績画面の誤字「４面」を「５面」に
void patch4men(unsigned char* const data) {
   if (prevRecord->name != "trophy.txt") {
      return;
   }
   if (data[11210] != 0x53) {
      return;
   }
   data[11210] = 0x54;
}

// オオワシ妖夢の与ダメージ上限を60から160に
void patchPlayer02C(unsigned char* const data) {
   if (prevRecord->name != "pl02c.sht") {
      return;
   }
   if (data[40] != 60) {
      return;
   }
   data[40] = 160;
}

// オオワシ魔理沙の爆風ダメージをカワウソ魔理沙と同値の16/13/11/10(power4/3/2/1)に変更
void patchPlayer01C(unsigned char* const data) {
   if (prevRecord->name != "pl01c.sht") {
      return;
   }
   if (data[0x00000BFA] != 10 || data[0x00000C52] != 10 || data[0x00000CAA] != 10
      || data[0x00000DB6] != 11 || data[0x00000E0E] != 11 || data[0x00000E66] != 11 || data[0x00000EBE] != 11)
   {
      return;
   }
   const unsigned int power3 = 11;
   const unsigned int power4 = 10;
   data[0x00000BFA] = power3;
   data[0x00000C52] = power3;
   data[0x00000CAA] = power3;
   data[0x00000DB6] = power4;
   data[0x00000E0E] = power4;
   data[0x00000E66] = power4;
   data[0x00000EBE] = power4;
}

void _stdcall patchFile(unsigned char* const data) {
   if (prevRecord == nullptr) {
      return;
   }
   patchPlayer02C(data);
   // patchPlayer01C(data); // 些細な違いすぎてリプレイ互換性壊すだけになりがちなのでdisable
   patchHayatsumi(data);
   patch4men(data);
}

bool patchFileDecode() {
   // 余白に自作コードをねじ込む
   {
      unsigned char newCode[] = {
         0x57,                         // PUSH EDI
         0x68, 0x00, 0x00, 0x00, 0x00, // PUSH 0x00000000(PUSH &patchFile)
         0x58,                         // POP EAX
         0xFF, 0xD0,                   // CALL EAX
         // ここから上書きで消した元のコード
         0x6A, 0x02,                   // PUSH 2
         0xE8, 0x50, 0xAF, 0xF6, 0xFF, // CALL 0x00404E20
         // 元のコードに戻す
         0xE9, 0xCB, 0x82, 0xF6, 0xFF, // JMP 0x004021A0
      };
      *reinterpret_cast<unsigned int *>(&newCode[2]) = reinterpret_cast<unsigned int>(&patchFile);
      unsigned char oldCode[_countof(newCode)] = {
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      };
      if (!ChangeCode(0x00499EC0, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // ファイルデコード処理直後から自作部分へ飛ばす
   {
      const unsigned char newCode[] = {
         0xE9, 0x22, 0x7D, 0x09, 0x00, // JMP 0x499EC0
         0x90,                         // NOP
         0x90,                         // NOP
      };
      unsigned char oldCode[_countof(newCode)] = {
         0x6A, 0x02,                   // PUSH 2
         0xE8, 0x80, 0x2C, 0x00, 0x00, // CALL 0x00404E20
      };
      if (!ChangeCode(0x402199, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   return true;
}

// ED呼び出しでmsgファイルとの対応を変更する
void _stdcall patchAchievementMain() {
   unsigned int &index = *reinterpret_cast<unsigned int*>(0x004B23E0);
   char * const filename = reinterpret_cast<char*>(0x52A810);
   const char* const map[] = {
      "e01.msg",
      "e02.msg",
      "e03.msg",
      "e04.msg",
      "e05.msg",
      "e06.msg",
      "e07.msg",
      "e08.msg",
      "e09.msg",
      "e10.msg",
      "e11.msg",
      "e12.msg",
   };
   if (_countof(map) > index) {
      ::strcpy_s(filename, 256, map[index]);
   }
}

// Extra会話呼び出しで自機タイプの対応を変更する
void _stdcall patchAchievementMain2(const unsigned int tropyId) {
   unsigned int &playerId = *reinterpret_cast<unsigned int *>(0x004B59F4);
   unsigned int &typeId = *reinterpret_cast<unsigned int*>(0x004B59F8);
   const unsigned int map[][2] = {
      // 0～11は来ないはずだが念のためオオカミ霊夢を入れておく
      {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
      {0, 0}, {0, 0},
      // 霊夢
      {0, 0}, {0, 1}, {0, 2},
      // 魔理沙
      {1, 0}, {1, 1}, {1, 2},
      // 妖夢
      {2, 0}, {2, 1}, {2, 2},
   };
   if (tropyId >= _countof(map)) {
      return;
   }
   playerId = map[tropyId][0];
   typeId = map[tropyId][1];
}

bool patchAchievement() {
   // 余白に自作コードをねじ込む
   {
      unsigned char newCode[] = {
         0x52,                         // PUSH EDX
         0x68, 0x00, 0x00, 0x00, 0x00, // PUSH 0x00000000(PUSH &patchAchievementMain)
         0x58,                         // POP EAX
         0xFF, 0xD0,                   // CALL EAX
         0x5A,                         // POP EDX
         // ここから上書きで消した元のコード
         0x6A, 0x00,                   // PUSH 0
         0xB9, 0x10, 0xA8, 0x52, 0x00, // MOV ECX, 0x0052A810
         // 元のコードに戻す
         0xE9, 0x9E, 0x21, 0xF8, 0xFF, // JMP 0x0041C094
      };
      *reinterpret_cast<unsigned int*>(&newCode[2]) = reinterpret_cast<unsigned int>(&patchAchievementMain);
      unsigned char oldCode[_countof(newCode)] = {
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      };
      if (!ChangeCode(0x00499EE0, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // .msg読み込み直前に自作部分へ飛ばす
   {
      const unsigned char newCode[] = {
         0xE9, 0x4E, 0xDE, 0x07, 0x00, // JMP 0x499EE0
         0x90,                         // NOP
         0x90,                         // NOP
      };
      unsigned char oldCode[_countof(newCode)] = {
         0x6A, 0x00,                   // PUSH 0
         0xB9, 0x10, 0xA8, 0x52, 0x00, // MOV ECX, 0x0052A810
      };
      if (!ChangeCode(0x0041C08D, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // グローバル変数のクリア処理を取り除く
   {
      const unsigned char newCode[] = {
         0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, // NOP
      };
      unsigned char oldCode[_countof(newCode)] = {
         0xC7, 0x05, 0xE0, 0x23, 0x4B, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, // MOV DWORD PTR DS:[0x4B23E0], -1
      };
      if (!ChangeCode(0x0041BFD1, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // 実績12番までED呼び出しルートに分岐させる
   {
      const unsigned char newCode[] = {
         0x83, 0xF9, 0x0B, // CMP 0x0A
      };
      unsigned char oldCode[_countof(newCode)] = {
         0x83, 0xF9, 0x08, // CMP 0x08
      };
      if (!ChangeCode(0x0045CB89, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // 余白に自作コードをねじ込む2
   {
      unsigned char newCode[] = {
         0x52,                         // PUSH EDX
         0xFF, 0x73, 0x24,             // PUSH DWORD PTR DS:[EBX+24]
         0x68, 0x00, 0x00, 0x00, 0x00, // PUSH 0x00000000(PUSH &patchAchievementMain2)
         0x58,                         // POP EAX
         0xFF, 0xD0,                   // CALL EAX
         0x5A,                         // POP EDX
         // 元のコードに戻す
         0xE9, 0xEC, 0x30, 0xFC, 0xFF, // JMP 0x0045CFFE
      };
      *reinterpret_cast<unsigned int*>(&newCode[5]) = reinterpret_cast<unsigned int>(&patchAchievementMain2);
      unsigned char oldCode[_countof(newCode)] = {
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      };
      if (!ChangeCode(0x00499F00, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // Extra会話呼び出し直前で自作部分へ飛ばす
   {
      const unsigned char newCode[] = {
         0xE9, 0x08, 0xCF, 0x03, 0x00, // JMP 0x499F00
         0x90,                         // NOP
         0x90,                         // NOP
         0x90,                         // NOP
         0x90,                         // NOP
         0x90,                         // NOP
         0x90,                         // NOP
      };
      unsigned char oldCode[_countof(newCode)] = {
         0x8B, 0x4B, 0x24,             // MOV ECX, DWORD PTR DS:[EBX*24]
         0xB8, 0x56, 0x55, 0x55, 0x55, // MOV EAX, 0x55555556
         0x83, 0xE9, 0x0C,             // SUB ECX, 0C
      };
      if (!ChangeCode(0x0045CFF3, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // 既定の自機設定を取り除く
   {
      const unsigned char newCode[] = {
         0x90, 0x90, 0x90, 0x90, 0x90, 0x90, // NOP
      };
      unsigned char oldCode[_countof(newCode)] = {
         0x89, 0x0D, 0xF4, 0x59, 0x4B, 0x00, // MOV DWORD PTR DS:[0x004B59F4], ECX
      };
      if (!ChangeCode(0x0045D02A, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // 既定の自機設定を取り除くその2
   {
      const unsigned char newCode[] = {
         0x90, 0x90, 0x90, 0x90, 0x90, // NOP
      };
      unsigned char oldCode[_countof(newCode)] = {
         0xA3, 0xF8, 0x59, 0x4B, 0x00, // MOV DWORD PTR DS:[0x004B59F8], EAX
      };
      if (!ChangeCode(0x0045D061, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   return true;
}

// replayフォルダ内にsnapshotフォルダが作成されるのを阻止
void _stdcall patchSnapshotMain() {
   ::SetCurrentDirectoryW(L"..\\");
}

bool patchSnapshot() {
   // 余白に自作コードをねじ込む
   {
      unsigned char newCode[] = {
         // 上書きした処理の代替処理
         0x5F,                         // POP EDI
         0x85, 0xC0,                   // TEST EAX
         0x74, 0x05,                   // JE SHORT 0x00499F2A
         0xE9, 0xA3, 0x7C, 0xFC, 0xFF, // JMP 0x00461BCD
         // 自作コードを実行
         0x57,                         // PUSH EDI
         0x68, 0x00, 0x00, 0x00, 0x00, // PUSH 0x00000000(PUSH &patchSnapshotMain)
         0x58,                         // POP EAX
         0xFF, 0xD0,                   // CALL EAX
         0x5F,                         // POP EDI
         // 元のコードに戻す
         0xE9, 0xA9, 0x7C, 0xFC, 0xFF, // JMP 0x00461BE2
      };
      *reinterpret_cast<unsigned int*>(&newCode[12]) = reinterpret_cast<unsigned int>(&patchSnapshotMain);
      unsigned char oldCode[_countof(newCode)] = {
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      };
      if (!ChangeCode(0x00499F20, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // セーブデータ初期化処理中に自作部分へ飛ばす
   {
      const unsigned char newCode[] = {
         0xE9, 0x53, 0x83, 0x03, 0x00, // JMP 0x00499F20
      };
      unsigned char oldCode[_countof(newCode)] = {
         0x5F,       // POP EDI
         0x85, 0xC0, // TEST EAX,EAX
         0x74, 0x15, // JE SHORT 0x00461BE2
      };
      if (!ChangeCode(0x00461BC8, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   return true;
}

// 実績を獲得する
void getTrophy(const unsigned int trophyId) {
   const auto proc = reinterpret_cast<void cdecl(*)(const unsigned int trophyId)>(0x00499F50);
   __asm {
      mov ecx, trophyId;
      call proc;
   }
}

// リプレイ中ならtrueを返す
bool isReplay() {
   const unsigned int baseAddr = *reinterpret_cast<const unsigned int*>(0x004B76B0);
   const unsigned int flag = *reinterpret_cast<const unsigned int*>(baseAddr + 0xA8);
   return flag != 0;
}

// replayフォルダ内にsnapshotフォルダが作成されるのを阻止
void _stdcall patchReplayAchievementMain(const unsigned int trophyId) {
   if (isReplay()) {
      return;
   }
   getTrophy(trophyId);
}

bool patchReplayAchievement() {
   // 余白に自作コードをねじ込む
   {
      unsigned char newCode[] = {
         // 自作コードを実行
         0x51,                         // PUSH ECX
         0x68, 0x00, 0x00, 0x00, 0x00, // PUSH 0x00000000(PUSH &patchSnapshotMain)
         0x58,                         // POP EAX
         0xFF, 0xD0,                   // CALL EAX
         // 元のコードに戻す
         0xC3,                         // RETN
      };
      *reinterpret_cast<unsigned int*>(&newCode[2]) = reinterpret_cast<unsigned int>(&patchReplayAchievementMain);
      unsigned char oldCode[_countof(newCode)] = {
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      };
      if (!ChangeCode(0x00499F40, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // 実績開放関数先頭から自作部分へ飛ばす
   {
      const unsigned char newCode[] = {
         0xE9, 0x9B, 0xC6, 0x03, 0x00, // JMP 0x00499F40
         0x90, 0x90, 0x90, 0x90,       // NOP
      };
      unsigned char oldCode[_countof(newCode)] = {
         0x55,                         // PUSH EBP
         0x8B, 0xEC,                   // MOV EBP, ESP
         0x51,                         // PUSH ECX
         0xA1, 0xDC, 0x77, 0x4B, 0x00, // MOV EAX, DWORD PTR DS:[0x004B77DC]
      };
      if (!ChangeCode(0x0045D8A0, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   // 余白に本来の実績解放関数先頭相当のコードをねじ込む
   {
      const unsigned char newCode[] = {
         0x55,                         // PUSH EBP
         0x8B, 0xEC,                   // MOV EBP, ESP
         0x51,                         // PUSH ECX
         0xA1, 0xDC, 0x77, 0x4B, 0x00, // MOV EAX, DWORD PTR DS:[0x004B77DC]
         0xE9, 0x4B, 0x39, 0xFC, 0xFF, // JMP 0x0045D8A9
      };
      const unsigned char oldCode[_countof(newCode)] = {
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      };
      if (!ChangeCode(0x00499F50, oldCode, newCode, sizeof(newCode))) {
         return false;
      }
   }
   return true;
}

void main() {
   // デバッグ用に黒窓を生やす
   //org::click3::DllHackLib::SetupConsole();

   ::printf("ChangeIAT: ");
   if (ChangeIAT(::GetModuleHandleW(nullptr))) {
      ::printf("Success\n");
   } else {
      ::printf("Failure\n");
   }

   ::printf("patchFileDecode: ");
   if (patchFileDecode()) {
      ::printf("Success\n");
   } else {
      ::printf("Failure\n");
   }

   ::printf("patchAchievement: ");
   if (patchAchievement()) {
      ::printf("Success\n");
   } else {
      ::printf("Failure\n");
   }

   ::printf("patchSnapshot: ");
   if (patchSnapshot()) {
      ::printf("Success\n");
   }
   else {
      ::printf("Failure\n");
   }

   ::printf("patchReplayAchievement: ");
   if (patchReplayAchievement()) {
      ::printf("Success\n");
   }
   else {
      ::printf("Failure\n");
   }
}
