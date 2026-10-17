#include "../feature_access.h"
int main() {
    if(!canUseBusinessContactSearch("VictorTang",true))return 1;
    if(!canUseBusinessContactSearch("victortang",true))return 2;
    for(const auto& name : {QString("Tjavs"),QString("phillip"),QString("VictorTang2"),QString("VictorTang admin"),QString()})
        if(canUseBusinessContactSearch(name,true))return 3;
    if(canUseBusinessContactSearch("VictorTang",false))return 4;
    return 0;
}
