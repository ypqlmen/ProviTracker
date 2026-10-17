#include "../order_product_selection.h"
int main(){
    OrderProductSelection selection;
    selection.setItems({{"mobil_25gb_36",8},{"til_datakort",2},{"mobil_25gb_36",3},{"retired-product",4}});
    if(selection.items().size()!=3 || selection.quantity("mobil_25gb_36")!=11 || selection.quantity("retired-product")!=4)return 1;
    selection.add("til_datakort");if(selection.quantity("til_datakort")!=3)return 2;
    selection.setQuantity("mobil_25gb_36",25);if(selection.quantity("mobil_25gb_36")!=25)return 3;
    selection.setQuantity("retired-product",0);if(selection.quantity("retired-product") || selection.items().size()!=2)return 4;
    selection.add("");selection.add("invalid",-1);if(selection.items().size()!=2)return 5;
    selection.setQuantity("large",std::numeric_limits<int>::max());selection.add("large");if(selection.quantity("large")!=std::numeric_limits<int>::max())return 6;
    const auto persisted=selection.items();selection.setItems(persisted);if(selection.items().size()!=3 || selection.quantity("mobil_25gb_36")!=25)return 7;
    selection.setItems(selection.items());if(selection.quantity("mobil_25gb_36")!=25)return 8;
    selection.setItems({});if(!selection.items().isEmpty())return 9;
    return 0;
}
