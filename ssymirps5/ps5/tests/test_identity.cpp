// SSYmirPS5: data-root and update-identity regression checks.
#include "OrbisPaths.h"
#include "ProsperoUpdate.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
int main(int argc,char** argv)
{
    assert(argc==2);
    if(std::string(argv[1])=="--default")
    {
        unsetenv("SSYMIRPS5_ROOT");
        assert(OrbisRoot()=="/data/ssymirps5");
        std::cout<<"Default root: /data/ssymirps5 PASS\n";
        return 0;
    }
    setenv("SSYMIRPS5_ROOT",argv[1],1);
    assert(OrbisPathsInit());
    const auto games=OrbisRomRoots(),bios=OrbisBiosDirs();
    assert(std::find(games.begin(),games.end(),OrbisDir("games"))!=games.end());
    assert(std::find(bios.begin(),bios.end(),OrbisDir("bios"))!=bios.end());
    std::ifstream paths(OrbisRoot()+"/paths.txt");
    const std::string text((std::istreambuf_iterator<char>(paths)),{});
    assert(text.find("/data/ssymirps5/games")!=std::string::npos);
    assert(text.find("/mnt/usbN/ssymirps5/games")!=std::string::npos);
    const std::string other_asset="OtherApp-v9.0.0-PPSA99378.zip";
    const std::string new_asset="SSYmirPS5-v9.0.0-PPSA99378.zip";
    auto release=[](const std::string& asset) {
        return "{\"tag_name\":\"v9.0.0\",\"assets\":[{\"name\":\""+asset+
            "\",\"browser_download_url\":\"https://example.invalid/app.zip\",\"size\":123}]}";
    };
    update::Release found;
    std::string why;
    assert(!update::ParseRelease(release(other_asset),"PPSA99378",found,&why,"SSYmirPS5-"));
    assert(update::ParseRelease(release(new_asset),"PPSA99378",found,&why,"SSYmirPS5-"));
    assert(found.zip_name==new_asset);
    assert(!update::ParseRelease(release("SSYmirPS5-v9.0.0-OTHER.zip"),"PPSA99378",found,&why,"SSYmirPS5-"));
    std::cout<<"Data/USB path templates and product-specific update archives PASS\n";
}
