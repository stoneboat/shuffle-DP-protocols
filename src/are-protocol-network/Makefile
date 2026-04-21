MCL_DIR = ../utils/mcl
EMP_DIR = ../utils/emp-tool/install
SSL_DIR = /opt/homebrew/opt/openssl
ARE_DIR = ../are-protocol
BIN_DIR = bin

CXX      = g++
CXXFLAGS = -std=c++17 -O2 \
           -I. -I$(ARE_DIR) -I$(ARE_DIR)/ot \
           -I$(MCL_DIR)/include \
           -I$(EMP_DIR)/include \
           -I$(SSL_DIR)/include \
           -maes -mpclmul

EMP_RPATH = -Wl,-rpath,$(abspath $(EMP_DIR)/lib)
LDFLAGS   = $(MCL_DIR)/lib/libmcl.a \
            -L$(EMP_DIR)/lib -lemp-tool $(EMP_RPATH) \
            -L$(SSL_DIR)/lib -lcrypto

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
	rm -rf $(BIN_DIR) $(TARGETS)

.PHONY: clean all
