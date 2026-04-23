BASE_DIR = $(shell cd .. && pwd)

MCL_DIR  = $(BASE_DIR)/utils/mcl/install
EMP_DIR  = $(BASE_DIR)/utils/emp-tool/install
ARE_DIR  = $(BASE_DIR)/are-protocol
BIN_DIR = bin

CXX      = g++
# CHANGED: -I$(EMP_DIR) instead of -I$(EMP_DIR)/include
# REMOVED: SSL_DIR include (not needed on most Linux clusters)
CXXFLAGS = -std=c++17 -O2 \
           -I. -I$(ARE_DIR) -I$(ARE_DIR)/ot \
           -I$(MCL_DIR)/include \
           -I$(EMP_DIR)/include \
           -march=native -maes -mpclmul -mssse3

# Use abspath to ensure the linker finds the absolute path for the rpath
EMP_RPATH = -Wl,-rpath,$(abspath $(EMP_DIR)/lib)

# REMOVED: -L$(SSL_DIR)/lib
LDFLAGS   = $(MCL_DIR)/lib/libmcl.a \
            -L$(EMP_DIR)/lib -lemp-tool $(EMP_RPATH) \
            -lcrypto -lpthread

HEADERS = net_serialize.h \
          $(ARE_DIR)/gc_protocol.h \
          $(ARE_DIR)/metrics.h \
          $(ARE_DIR)/ot/rabin_ot_are.h \
          $(ARE_DIR)/ot/string_ot_are.h \
          $(ARE_DIR)/ot/permxor_are.h \
          $(ARE_DIR)/circuits/build_and_chain.h \
          $(ARE_DIR)/circuits/build_bitonic_sort.h \
          $(ARE_DIR)/circuits/build_dp_selection.h \
          $(ARE_DIR)/circuits/build_sequential_sum.h \
          $(ARE_DIR)/circuits/build_tree_sum.h \
          $(ARE_DIR)/circuits/build_tree_mechanism.h \
          $(ARE_DIR)/circuits/build_linear_contextual.h \
          $(ARE_DIR)/circuits/build_distinct_elements.h \
          $(ARE_DIR)/circuits/build_distinct_elements_histogram.h

TARGETS = evaluator client gen_tables
BIN_TARGETS = $(addprefix $(BIN_DIR)/,$(TARGETS))

all: $(BIN_TARGETS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/evaluator: evaluator.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR)/client: client.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR)/gen_tables: gen_tables.cpp $(HEADERS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)

.PHONY: clean all