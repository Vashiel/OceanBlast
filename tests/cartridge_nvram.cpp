#include "core/cartridge_nvram.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>
using namespace oceanblast;
int main(int argc,char**argv) {
    assert(argc<=2);
    const auto unique=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root=argc==2 ? std::filesystem::path(argv[1]) :
        std::filesystem::temp_directory_path()/("oceanblast-nvram-"+unique);
    assert(!std::filesystem::exists(root));std::filesystem::create_directories(root);
    const auto first=root/"first.rom",renamed=root/"renamed.rom",other=root/"other.rom";
    {std::ofstream f(first);f<<"123456789";}
    std::filesystem::copy_file(first,renamed);
    {std::ofstream f(other);f<<"different";}
    std::array<uint8_t,2048> legacy{};legacy.fill(0x53);legacy[4]=80;
    {std::ofstream f(root/"board.nvram",std::ios::binary);f.write(reinterpret_cast<const char*>(legacy.data()),legacy.size());}
    const auto a=prepareCartridgeNvram(root,first),b=prepareCartridgeNvram(root,renamed),c=prepareCartridgeNvram(root,other);
    assert(a.filename()=="9-cbf43926.nvram");assert(a==b&&a!=c);
    {std::fstream f(a,std::ios::in|std::ios::out|std::ios::binary);f.put(char(42));}
    assert(prepareCartridgeNvram(root,first)==a);
    {std::ifstream f(a,std::ios::binary);assert(f.get()==42);}
    {std::ifstream f(c,std::ios::binary);assert(f.get()==0x53);}
    {std::ifstream f(root/"board.nvram",std::ios::binary);std::array<uint8_t,2048> read{};f.read(reinterpret_cast<char*>(read.data()),read.size());assert(read==legacy);}
    auto repaired=legacy;migrateCartridgeSettings(repaired,17301504,0x93209877u);
    assert(repaired[4]==1);repaired[4]=legacy[4];assert(repaired==legacy);
    for(uint8_t difficulty=0;difficulty<=2;++difficulty) {
        repaired=legacy;repaired[4]=difficulty;const auto before=repaired;
        migrateCartridgeSettings(repaired,17301504,0x93209877u);assert(repaired==before);
    }
    repaired=legacy;migrateCartridgeSettings(repaired,17301504,0xeba2a2c3u);assert(repaired==legacy);
    {std::ofstream f(c,std::ios::binary|std::ios::trunc);f.put(0);}
    bool failed=false;try{prepareCartridgeNvram(root,other);}catch(const std::runtime_error&){failed=true;}assert(failed);
    for(const auto& path:{first,renamed,other,root/"board.nvram",a,c})std::filesystem::remove(path);
    std::filesystem::remove(root/"nvram");std::filesystem::remove(root);
    std::cout<<"Content identity, renamed ROM, separate writes, legacy preservation, selective Chefs migration, valid difficulties and malformed save checks passed.\n";
}
