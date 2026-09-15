#pragma once

#include "ast/ast.h"
#include "defines/defines.h"
#include "lexer/token.h"

attribute_const AstBinaryKind AstBinaryKind_FromTokenType(AcuTokenType);

attribute_const AstUnaryKind AstUnaryKind_FromTokenType(AcuTokenType);

attribute_const AstAssignKind AstAssignKind_FromTokenType(AcuTokenType);
