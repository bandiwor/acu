CCompiler = clang
LLVM_PROFDATA ?= llvm-profdata

VM_TARGET = acu
COMP_TARGET = acuc
TEST_TARGET = acu_test

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BUILD_DIR = build

LIB_SRCS = $(shell find $(SRC_DIR) -name '*.c' ! -name 'main.c')
VM_MAIN_SRC = $(SRC_DIR)/vm/main.c
COMP_MAIN_SRC = $(SRC_DIR)/compiler/main.c
TEST_SRCS = $(shell find $(TEST_DIR) -name '*.c')

CFLAGS_COMMON = -I$(INC_DIR) -Wall -Wextra -Wpedantic -Wshadow -Werror=implicit-function-declaration -std=gnu11 -march=native -MMD -MP
LDFLAGS_COMMON = -lm

CFLAGS_RELEASE = $(CFLAGS_COMMON) -O3 -DNDEBUG -flto
LDFLAGS_RELEASE = $(LDFLAGS_COMMON) -flto

CFLAGS_DEBUG = $(CFLAGS_COMMON) -O0 -g3 -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS_DEBUG = $(LDFLAGS_COMMON) -fsanitize=address,undefined

# -----------------------------------------------------------------------------
# Директории и флаги PGO
# -----------------------------------------------------------------------------
PGO_DIR = $(BUILD_DIR)/pgo
PGO_RAW_DIR = $(PGO_DIR)/raw
PGO_DATA = $(PGO_DIR)/acu.profdata
BENCH_ACU ?= example/pgo.acu

CLANG_PGO_FLAGS = -mllvm -enable-value-profiling=false

CFLAGS_PGO_GEN = $(CFLAGS_RELEASE) -fprofile-generate=$(PGO_RAW_DIR) $(CLANG_PGO_FLAGS)
LDFLAGS_PGO_GEN = $(LDFLAGS_RELEASE) -fprofile-generate=$(PGO_RAW_DIR)

CFLAGS_PGO_USE = $(CFLAGS_RELEASE) -fprofile-use=$(PGO_DATA) $(CLANG_PGO_FLAGS)
LDFLAGS_PGO_USE = $(LDFLAGS_RELEASE) -fprofile-use=$(PGO_DATA)

PGO_GEN_DIR = $(PGO_DIR)/gen
PGO_USE_DIR = $(PGO_DIR)/use

REL_DIR = $(BUILD_DIR)/release
DBG_DIR = $(BUILD_DIR)/debug

REL_LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(REL_DIR)/src/%.o)
REL_VM_OBJ   = $(VM_MAIN_SRC:$(SRC_DIR)/%.c=$(REL_DIR)/src/%.o)
REL_COMP_OBJ = $(COMP_MAIN_SRC:$(SRC_DIR)/%.c=$(REL_DIR)/src/%.o)

DBG_LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(DBG_DIR)/src/%.o)
DBG_VM_OBJ   = $(VM_MAIN_SRC:$(SRC_DIR)/%.c=$(DBG_DIR)/src/%.o)
DBG_COMP_OBJ = $(COMP_MAIN_SRC:$(SRC_DIR)/%.c=$(DBG_DIR)/src/%.o)

PGO_GEN_LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(PGO_GEN_DIR)/src/%.o)
PGO_GEN_VM_OBJ   = $(VM_MAIN_SRC:$(SRC_DIR)/%.c=$(PGO_GEN_DIR)/src/%.o)

PGO_USE_LIB_OBJS = $(LIB_SRCS:$(SRC_DIR)/%.c=$(PGO_USE_DIR)/src/%.o)
PGO_USE_VM_OBJ   = $(VM_MAIN_SRC:$(SRC_DIR)/%.c=$(PGO_USE_DIR)/src/%.o)

DBG_TEST_OBJS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(DBG_DIR)/tests/%.o)

ALL_OBJS = $(REL_LIB_OBJS) $(REL_VM_OBJ) $(REL_COMP_OBJ) \
           $(DBG_LIB_OBJS) $(DBG_VM_OBJ) $(DBG_COMP_OBJ) \
           $(PGO_GEN_LIB_OBJS) $(PGO_GEN_VM_OBJ) \
           $(PGO_USE_LIB_OBJS) $(PGO_USE_VM_OBJ) \
           $(DBG_TEST_OBJS)

.PHONY: all release debug test clean clean-release run format compiledb \
        acu acuc vm compiler \
        acu-release acuc-release release-acu release-acuc \
        acu-debug acuc-debug debug-acu debug-acuc \
        pgo acu-pgo vm-pgo

all: release

release: $(REL_DIR)/$(VM_TARGET) $(REL_DIR)/$(COMP_TARGET)
debug: $(DBG_DIR)/$(VM_TARGET) $(DBG_DIR)/$(COMP_TARGET)

# Таргеты сборки с профильной оптимизацией (PGO)
pgo: acu-pgo
vm-pgo: acu-pgo

# Удобные короткие таргеты для релизной сборки
acu: $(REL_DIR)/$(VM_TARGET)
acuc: $(REL_DIR)/$(COMP_TARGET)
vm: acu
compiler: acuc
acu-release: acu
acuc-release: acuc
release-acu: acu
release-acuc: acuc

# Удобные таргеты для дебаг сборки
acu-debug: $(DBG_DIR)/$(VM_TARGET)
acuc-debug: $(DBG_DIR)/$(COMP_TARGET)
debug-acu: acu-debug
debug-acuc: acuc-debug
vm-debug: acu-debug
compiler-debug: acuc-debug

# -----------------------------------------------------------------------------
# PGO Pipeline: 1. Инструментирование -> 2. Тренировка -> 3. Финальная линковка
# -----------------------------------------------------------------------------

# Шаг 1: Сборка инструментального бинарника VM
$(PGO_DIR)/instrumented/$(VM_TARGET): $(PGO_GEN_LIB_OBJS) $(PGO_GEN_VM_OBJ)
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_PGO_GEN) $^ -o $@ $(LDFLAGS_PGO_GEN)

$(PGO_DATA): $(PGO_DIR)/instrumented/$(VM_TARGET) $(REL_DIR)/$(COMP_TARGET)
	@echo "🏋️ [PGO 1/3] Запуск тренировочной нагрузки на $(BENCH_ACU)..."
	@mkdir -p $(PGO_RAW_DIR)
	@rm -f $(PGO_RAW_DIR)/*.profraw
	@if [ -f "$(BENCH_ACU)" ]; then \
		BENCH_BIN="$(PGO_DIR)/bench.acub"; \
		./$(REL_DIR)/$(COMP_TARGET) $(BENCH_ACU) -o $$BENCH_BIN 2>/dev/null || ./$(REL_DIR)/$(COMP_TARGET) $(BENCH_ACU); \
		if [ ! -f "$$BENCH_BIN" ] && [ -f "out.acuc" ]; then \
			mv out.acuc $$BENCH_BIN; \
		elif [ ! -f "$$BENCH_BIN" ]; then \
			ALT_BIN=$$(echo "$(BENCH_ACU)" | sed 's/\.acu$$/\.acub/'); \
			if [ -f "$$ALT_BIN" ]; then mv $$ALT_BIN $$BENCH_BIN; fi; \
		fi; \
		if [ ! -f "$$BENCH_BIN" ]; then \
			echo "❌ Ошибка: скомпилированный файл байт-кода не найден!"; exit 1; \
		fi; \
		LLVM_PROFILE_FILE="$(PGO_RAW_DIR)/acu-%m.profraw" ./$(PGO_DIR)/instrumented/$(VM_TARGET) $$BENCH_BIN > /dev/null; \
		rm -f $$BENCH_BIN out.acuc; \
	else \
		echo "⚠️ Файл бенчмарка $(BENCH_ACU) не найден. Укажите BENCH_ACU=<путь>"; exit 1; \
	fi
	@echo "📊 [PGO 2/3] Объединение сырых данных профиля..."
	@$(LLVM_PROFDATA) merge -output=$@ $(PGO_RAW_DIR)/*.profraw

# Шаг 3: Финальная линковка VM с использованием профиля
acu-pgo: $(PGO_DATA) $(PGO_USE_LIB_OBJS) $(PGO_USE_VM_OBJ)
	@mkdir -p $(REL_DIR)
	$(CCompiler) $(CFLAGS_PGO_USE) $(PGO_USE_LIB_OBJS) $(PGO_USE_VM_OBJ) -o $(REL_DIR)/$(VM_TARGET) $(LDFLAGS_PGO_USE)
	@echo "🚀 [PGO 3/3] Сборка Release PGO (VM Acu) завершена: $(REL_DIR)/$(VM_TARGET)"

# Компиляция объектов для PGO (Фаза генерации профиля)
$(PGO_GEN_DIR)/src/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_PGO_GEN) -c $< -o $@

# Компиляция объектов для PGO (Фаза применения профиля)
$(PGO_USE_DIR)/src/%.o: $(SRC_DIR)/%.c $(PGO_DATA)
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_PGO_USE) -c $< -o $@

# -----------------------------------------------------------------------------
# Стандартная сборка Release / Debug
# -----------------------------------------------------------------------------

# Линковка Release
$(REL_DIR)/$(VM_TARGET): $(REL_LIB_OBJS) $(REL_VM_OBJ)
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_RELEASE) $^ -o $@ $(LDFLAGS_RELEASE)
	@echo "✅ Сборка Release (VM Acu) завершена: $@"

$(REL_DIR)/$(COMP_TARGET): $(REL_LIB_OBJS) $(REL_COMP_OBJ)
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_RELEASE) $^ -o $@ $(LDFLAGS_RELEASE)
	@echo "✅ Сборка Release (Compiler Acu) завершена: $@"

# Линковка Debug
$(DBG_DIR)/$(VM_TARGET): $(DBG_LIB_OBJS) $(DBG_VM_OBJ)
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_DEBUG) $^ -o $@ $(LDFLAGS_DEBUG)
	@echo "🐛 Сборка Debug (VM Acu) завершена: $@"

$(DBG_DIR)/$(COMP_TARGET): $(DBG_LIB_OBJS) $(DBG_COMP_OBJ)
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_DEBUG) $^ -o $@ $(LDFLAGS_DEBUG)
	@echo "🐛 Сборка Debug (Compiler Acu) завершена: $@"

# Компиляция объектов исходников
$(REL_DIR)/src/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_RELEASE) -c $< -o $@

$(DBG_DIR)/src/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_DEBUG) -c $< -o $@

# Тесты
test: $(DBG_DIR)/$(TEST_TARGET)
	@./$(DBG_DIR)/$(TEST_TARGET)

$(DBG_DIR)/$(TEST_TARGET): $(DBG_LIB_OBJS) $(DBG_TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_DEBUG) $^ -o $@ $(LDFLAGS_DEBUG)
	@echo "🧪 Сборка тестов завершена: $@"

$(DBG_DIR)/tests/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CCompiler) $(CFLAGS_DEBUG) -c $< -o $@

run: $(DBG_DIR)/$(VM_TARGET)
	@echo "⚡ Запуск Acu VM..."
	@./$(DBG_DIR)/$(VM_TARGET)

format:
	@echo "🎨 Форматирование кода..."
	@clang-format -i $(shell find $(SRC_DIR) $(TEST_DIR) $(INC_DIR) -name '*.[ch]')

compiledb:
	@echo "📝 Генерация compile_commands.json для clangd..."
	@make clean
	@bear -- make debug

clean-release:
	@echo "🧹 Очистка Release и PGO..."
	@rm -rf $(REL_DIR) $(PGO_DIR)

clean:
	@echo "🧹 Полная очистка..."
	@rm -rf $(BUILD_DIR) compile_commands.json .cache

-include $(ALL_OBJS:.o=.d)
