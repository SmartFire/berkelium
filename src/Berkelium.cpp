
#include "chrome/browser/tab_contents/tab_contents.h"
#include "berkelium/Berkelium.hpp"
#include "berkelium/Root.hpp"
namespace Berkelium {

void init() {
    new Root();
}
void destroy() {
    Root::destroy();
}


int renderToBuffer(char * buffer, int width, int height) {
    return 0;
}

}
