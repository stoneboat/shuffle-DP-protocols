from __future__ import annotations

from dataclasses import dataclass
from typing import List, Tuple, Optional, Dict
import os
import numpy as np


@dataclass
class CircuitGraph:
    """
    Faithful circuit/netlist view:
      - gates are 1-indexed: 1..num_gates
      - directed pin-level edges correspond to fanout connections u -> v
    """
    num_gates: int                       # m
    gate_type: List[str]                 # length m+1, gate_type[g]
    fanin: List[int]                     # length m+1, fanin[g]
    out_edges: List[List[Tuple[int,int]]]  # out_edges[g] = [(dst_gate, dst_pin), ...]
    # optional: output sinks
    out_to_outputbits: Optional[List[List[int]]] = None  # list per gate: output bit indices it drives
    total_input_bits: Optional[int] = None  # total number of input bits from output.inputs.partyA.txt

    @staticmethod
    def from_cbmc_gc_gate_file(path: str) -> "CircuitGraph":
        """
        Parse output.gate.txt (CBMC-GC-2 fanout netlist).
        Expected line format (conceptually):
          TYPE fanin outPin:destId:destPin outPin:destId:destPin ...
        We build edges only for destId > 0 (gate-to-gate).
        """
        gate_type: List[str] = [""]
        fanin: List[int] = [0]
        out_edges: List[List[Tuple[int,int]]] = [[]]  # dummy for 1-indexing
        out_to_outputbits: List[List[int]] = [[]]

        with open(path, "r") as f:
            for line_no, line in enumerate(f, start=1):
                line = line.strip()
                if not line:
                    continue
                toks = line.split()
                gtype = toks[0]
                k = int(toks[1])

                gate_type.append(gtype)
                fanin.append(k)
                out_edges.append([])
                out_to_outputbits.append([])

                # Remaining tokens are fanout specs: outPin:destId:destPin
                for t in toks[2:]:
                    parts = t.split(":")
                    if len(parts) != 3:
                        # If your build emits occasional non-triplet tokens, handle here.
                        continue
                    _out_pin = int(parts[0])          # usually 0
                    dest_id = int(parts[1])
                    dest_pin = int(parts[2])

                    if dest_id > 0:
                        out_edges[line_no].append((dest_id, dest_pin))
                    else:
                        out_to_outputbits[line_no].append(-dest_id)

        num_gates = len(gate_type) - 1

        # Try to load total_input_bits from output.inputs.partyA.txt in the same directory
        total_input_bits = None
        gate_file_dir = os.path.dirname(os.path.abspath(path))
        party_a_file = os.path.join(gate_file_dir, "output.inputs.partyA.txt")
        
        if os.path.exists(party_a_file):
            try:
                with open(party_a_file, "r") as f:
                    first_line = f.readline().strip()
                    if first_line:
                        parts = first_line.split()
                        if len(parts) >= 3:
                            total_input_bits = int(parts[2])
            except (ValueError, IOError) as e:
                # If we can't read or parse the file, just continue without it
                pass
        
        return CircuitGraph(
            num_gates=num_gates,
            gate_type=gate_type,
            fanin=fanin,
            out_edges=out_edges,
            out_to_outputbits=out_to_outputbits,
            total_input_bits=total_input_bits,
        )

    def nonxor_weight(self, xor_is_free: bool = True) -> np.ndarray:
        """
        Return node weights w[g] used for balance.
        Default: XOR/NOT gates cost 0, AND/OR (and other nonlinear gates) cost 1.
        """
        free_gates = {"XOR", "NOT"} if xor_is_free else set()

        w = np.zeros(self.num_gates + 1, dtype=np.int32)
        for g in range(1, self.num_gates + 1):
            gtype = self.gate_type[g].upper()
            if gtype in free_gates:
                w[g] = 0
            else:
                w[g] = 1
        return w

    def to_partitionable(
        self,
        weight: Optional[np.ndarray] = None,
        compress_undirected: bool = True
    ) -> "PartitionableGraph":
        """
        Build a compact representation for partitioners and metrics:
          - directed edge arrays (src,dst) for exact pin-level cut metrics
          - undirected CSR adjacency (xadj,adjncy,adjwgt) for METIS/KaHIP-like tools
        """
        m = self.num_gates
        if weight is None:
            weight = self.noxor_weight = self.nonxor_weight()

        # ---- directed pin-level edge list ----
        src_list: List[int] = []
        dst_list: List[int] = []
        dstpin_list: List[int] = []

        for u in range(1, m + 1):
            for (v, pin) in self.out_edges[u]:
                src_list.append(u)
                dst_list.append(v)
                dstpin_list.append(pin)

        src = np.array(src_list, dtype=np.int32)
        dst = np.array(dst_list, dtype=np.int32)
        dst_pin = np.array(dstpin_list, dtype=np.int16)

        # ---- undirected adjacency for partitioner ----
        # METIS uses 0-indexed nodes; we convert gate ids 1..m -> 0..m-1.
        u0 = src - 1
        v0 = dst - 1

        if compress_undirected:
            # Build weights = multiplicity of connections between {u,v}
            # Key is ordered pair (min,max)
            a = np.minimum(u0, v0)
            b = np.maximum(u0, v0)
            keys = a.astype(np.int64) * m + b.astype(np.int64)
            uniq_keys, counts = np.unique(keys, return_counts=True)

            uu = (uniq_keys // m).astype(np.int32)
            vv = (uniq_keys % m).astype(np.int32)

            # Now create symmetric adjacency (uu->vv and vv->uu) with same weight
            nbr_u = np.concatenate([uu, vv])
            nbr_v = np.concatenate([vv, uu])
            nbr_w = np.concatenate([counts, counts]).astype(np.int32)
        else:
            # Keep duplicates as unit weights (less ideal for partitioners)
            nbr_u = np.concatenate([u0, v0]).astype(np.int32)
            nbr_v = np.concatenate([v0, u0]).astype(np.int32)
            nbr_w = np.ones_like(nbr_u, dtype=np.int32)

        # Build CSR
        # Sort by source node
        order = np.argsort(nbr_u, kind="stable")
        nbr_u = nbr_u[order]
        nbr_v = nbr_v[order]
        nbr_w = nbr_w[order]

        xadj = np.zeros(m + 1, dtype=np.int32)
        # count degrees
        deg = np.bincount(nbr_u, minlength=m)
        xadj[1:] = np.cumsum(deg).astype(np.int32)

        adjncy = nbr_v.astype(np.int32)
        adjwgt = nbr_w.astype(np.int32)

        # Node weights for partitioner should be 0-indexed length m
        vwgt = weight[1:].astype(np.int32)

        return PartitionableGraph(
            num_gates=m,
            vwgt=vwgt,
            src=src,
            dst=dst,
            dst_pin=dst_pin,
            xadj=xadj,
            adjncy=adjncy,
            adjwgt=adjwgt,
            total_input_bits=self.total_input_bits,
        )


@dataclass
class PartitionableGraph:
    """
    Export/analysis representation:
      - vwgt: node weights (length m, 0-indexed) for balance
      - (xadj, adjncy, adjwgt): undirected CSR for partitioners
      - (src, dst): directed pin-level edges for exact cut statistics
    """
    num_gates: int                      # m
    vwgt: np.ndarray                    # shape (m,), node weights (non-XOR count, etc.)

    # Directed pin-level edge list (1-indexed in src/dst for convenience with gate ids)
    src: np.ndarray                     # shape (E,), entries in 1..m
    dst: np.ndarray                     # shape (E,), entries in 1..m
    dst_pin: Optional[np.ndarray] = None

    # Undirected CSR (0-indexed)
    xadj: Optional[np.ndarray] = None   # shape (m+1,)
    adjncy: Optional[np.ndarray] = None # shape (nnz,)
    adjwgt: Optional[np.ndarray] = None # shape (nnz,)
    total_input_bits: Optional[int] = None  # total number of input bits from output.inputs.partyA.txt

    def cut_size_pin(self, part: np.ndarray) -> int:
        """
        Pin-level directed cut: count edges u->v with part[u] != part[v].
        part is 0-indexed length m, where part[g-1] is the part of gate g.
        """
        pu = part[self.src - 1]
        pv = part[self.dst - 1]
        return int(np.sum(pu != pv))

    def outgoing_boundary_per_part(self, part: np.ndarray, nparts: int) -> np.ndarray:
        """
        For each part i: number of outgoing pin-level edges leaving that part.
        """
        pu = part[self.src - 1]
        pv = part[self.dst - 1]
        mask = (pu != pv)
        counts = np.bincount(pu[mask], minlength=nparts)
        return counts.astype(np.int64)

    def incoming_boundary_per_part(self, part: np.ndarray, nparts: int) -> np.ndarray:
        """
        For each part i: number of incoming pin-level edges entering that part.
        """
        pu = part[self.src - 1]
        pv = part[self.dst - 1]
        mask = (pu != pv)
        counts = np.bincount(pv[mask], minlength=nparts)
        return counts.astype(np.int64)

    def load_per_part(self, part: np.ndarray, nparts: int) -> np.ndarray:
        """
        Sum of node weights (e.g., non-XOR count) per part.
        """
        return np.bincount(part, weights=self.vwgt, minlength=nparts)

    def summary_metrics(self, part: np.ndarray, nparts: int) -> Dict[str, object]:
        """
        Convenience summary: cut, boundary stats (in/out/cross), load imbalance.
        """
        cut = self.cut_size_pin(part)
        outb = self.outgoing_boundary_per_part(part, nparts)
        inb = self.incoming_boundary_per_part(part, nparts)
        load = self.load_per_part(part, nparts)
        # cross boundary keeps both directions (out, in) for each part
        cross_boundary = (
            np.stack([outb, inb], axis=1)
            if nparts
            else np.empty((0, 2), dtype=np.int64)
        )
        total_nonfree = int(self.vwgt.sum()) if self.vwgt.size else 0

        client_garbling_computation_cost = load * 3 * 1e-9 # 3 ns per gate
        client_garbling_communication_cost = load * 24 * 1e-6 # 192 bits per gate

        assert self.total_input_bits is not None, "total_input_bits is not set"
        client_input_bits = self.total_input_bits / nparts
        client_ARE_computation_cost = (outb + inb + client_input_bits) * 0.01
        client_ARE_communication_cost = (outb + inb + client_input_bits) * 381 * 1e-6 # 3048 bits per gate

        client_balance_computation_cost = client_garbling_computation_cost + client_ARE_computation_cost
        client_balance_communication_cost = client_garbling_communication_cost + client_ARE_communication_cost

        baseline_computation_cost = total_nonfree * 3 * 1e-9 + self.total_input_bits * 0.01
        baseline_communication_cost = total_nonfree * 24 * 1e-6 + self.total_input_bits * 381 * 1e-6
        return {
            "cut_pin": cut,
            "max_out_boundary": int(outb.max()) if outb.size else 0,
            "max_in_boundary": int(inb.max()) if inb.size else 0,
            "out_boundary": outb,
            "in_boundary": inb,
            "cross_boundary": cross_boundary,
            "max_cross_boundary": int((outb + inb).max()) if outb.size else 0,
            "load": load,
            "max_load": float(load.max()) if load.size else 0.0,
            "min_load": float(load.min()) if load.size else 0.0,
            "avg_load": float(load.mean()) if load.size else 0.0,
            "total_nonfree": total_nonfree,
            "max_client_balance_computation_cost (s)": float(client_balance_computation_cost.max()) if client_balance_computation_cost.size else 0.0,
            "max_client_balance_communication_cost (MB)": float(client_balance_communication_cost.max()) if client_balance_communication_cost.size else 0.0,
            "baseline computation cost (s)": float(baseline_computation_cost) if total_nonfree > 0 else 0.0,
            "baseline communication cost (MB)": float(baseline_communication_cost) if total_nonfree > 0 else 0.0,
        }
