MCL_DIR = ../utils/mcl
EMP_DIR = ../utils/emp-tool/install
SSL_DIR = /opt/homebrew/opt/openssl

CXX      = g++
CXXFLAGS = -std=c++17 -O2 \
           -I. -Iot \
           -I$(MCL_DIR)/include \
           -I$(EMP_DIR)/include \
           -I$(SSL_DIR)/include \
           -maes -mpclmul

# Link: mcl static + emp-tool dylib + OpenSSL
EMP_RPATH = -Wl,-rpath,$(abspath $(EMP_DIR)/lib)
LDFLAGS   = $(MCL_DIR)/lib/libmcl.a \
            -L$(EMP_DIR)/lib -lemp-tool $(EMP_RPATH) \
            -L$(SSL_DIR)/lib -lcrypto

CIRCUIT_HEADERS = circuits/build_and_chain.h circuits/build_bitonic_sort.h \
                  circuits/build_dp_selection.h circuits/build_sequential_sum.h \
                  circuits/build_tree_sum.h circuits/build_tree_mechanism.h \
                  circuits/build_linear_contextual.h \
                  circuits/build_distinct_elements.h
HEADERS = ot/rabin_ot_are.h ot/string_ot_are.h ot/permxor_are.h metrics.h gc_protocol.h $(CIRCUIT_HEADERS)

TARGETS = rabin_ot_are string_ot_are permxor_are gc_emp_are benchmark

all: $(TARGETS)

rabin_ot_are: test/rabin_ot_are.cpp ot/rabin_ot_are.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

string_ot_are: test/string_ot_are.cpp ot/string_ot_are.h ot/rabin_ot_are.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

permxor_are: test/permxor_are.cpp ot/permxor_are.h ot/string_ot_are.h ot/rabin_ot_are.h
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

gc_emp_are: gc_emp_are.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

benchmark: benchmark.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: clean all
