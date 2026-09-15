#include "types/acu_position.h"

attribute_const AcuPosition AcuPosition_FromToken(AcuToken token, FileId file) {
    return (AcuPosition){
        .offset = token.offset,
        .length = token.length,
        .file = file,
    };
}
