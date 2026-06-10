from __future__ import annotations
from dataclasses import dataclass
from typing import Protocol, Optional
import numpy as np
from boolean_circuit.graph_synthesizer import PartitionableGraph
import kahip

class Partitioner(Protocol):
    def partition(self, g: "PartitionableGraph", nparts: int, gamma: float = 0.0) -> np.ndarray:
        """
        Return part array of length g.num_gates, 0-indexed part IDs in [0, nparts-1].
        gamma is a soft parameter here: the baseline aims for balanced loads but does not
        guarantee strict gamma-feasibility in all cases (e.g., indivisible heavy nodes).
        """
        ...

@dataclass
class BalancedContiguousPartitioner:
    """
    Baseline: assign gates in order (0..m-1) into nparts contiguous blocks,
    approximately equalizing total node weight per part.

    This does NOT optimize cut; it is an initial partition useful for evaluation
    and as a starting point for later refinement.
    """
    order: str = "gate_id"  # placeholder for future variants

    def partition(self, g: "PartitionableGraph", nparts: int, gamma: float = 0.0) -> np.ndarray:
        m = g.num_gates
        assert nparts >= 1
        assert m >= 1

        vwgt = g.vwgt.astype(np.int64)  # length m
        W = int(vwgt.sum())

        # Trivial case: no weighted work at all (all XOR if using non-XOR weights)
        # Fall back to equal-sized contiguous blocks by gate count.
        if W == 0:
            part = np.empty(m, dtype=np.int32)
            # roughly equal number of nodes
            base = m // nparts
            rem = m % nparts
            idx = 0
            for p in range(nparts):
                cnt = base + (1 if p < rem else 0)
                part[idx:idx+cnt] = p
                idx += cnt
            return part

        # Target weight per part (float), and a cap if gamma is used as a soft limit.
        target = W / nparts
        cap = (1.0 + gamma) * target if gamma > 0 else None

        part = np.empty(m, dtype=np.int32)

        p = 0
        acc = 0.0  # accumulated weight in current part (float for smoother split)

        for i in range(m):
            w = float(vwgt[i])

            # If we are at the last part, put everything remaining there.
            if p == nparts - 1:
                part[i] = p
                continue

            # Decide whether to cut before placing node i.
            # Rule: if placing i would move us farther from target than cutting now, cut now.
            # Also, if a soft cap is provided and we'd exceed it, cut now (when possible).
            would_be = acc + w
            cut_now = False

            # Soft cap check
            if cap is not None and would_be > cap and acc > 0:
                cut_now = True
            else:
                # "closest to target" check
                # Compare distance if we keep i vs if we start new part at i.
                keep_dist = abs(target - would_be)
                cut_dist  = abs(target - acc)
                # cut if keeping i makes us worse and we have at least some content
                if (keep_dist > cut_dist) and (acc > 0):
                    cut_now = True

            if cut_now:
                p += 1
                acc = 0.0

            part[i] = p
            acc += w

        return part


@dataclass
class KaHIPPartitioner:
    """
    Partition using KaHIP (KaFFPa).
    Expects pg.xadj, pg.adjncy, pg.adjwgt to be an undirected CSR representation
    (0-indexed) and pg.vwgt to be node weights (length m).
    """
    mode: int = 0              # 0=FAST, 1=ECO, 2=STRONG in KaHIP examples
    seed: int = 0
    suppress_output: int = 1   # 1 to silence KaHIP output

    def partition(self, pg: "PartitionableGraph", nparts: int, gamma: float = 0.03) -> np.ndarray:
        """
        Returns:
          part: np.ndarray shape (m,), dtype=int32, values in [0, nparts-1]
        """
        if pg.xadj is None or pg.adjncy is None:
            raise ValueError("KaHIPPartitioner requires CSR arrays: xadj/adjncy (and optionally adjwgt).")
        if pg.vwgt is None:
            raise ValueError("KaHIPPartitioner requires node weights pg.vwgt (length m).")

        m = pg.num_gates
        if len(pg.vwgt) != m:
            raise ValueError(f"Expected pg.vwgt length {m}, got {len(pg.vwgt)}.")

        # KaHIP wants integer lists (Python lists are safest for bindings).
        vwgt = pg.vwgt.astype(np.int32).tolist()
        xadj = pg.xadj.astype(np.int32).tolist()
        adjncy = pg.adjncy.astype(np.int32).tolist()

        # Edge weights (adjcwgt) are optional in some builds; if missing, use all-ones.
        if pg.adjwgt is None:
            adjcwgt = [1] * len(adjncy)
        else:
            adjcwgt = pg.adjwgt.astype(np.int32).tolist()

        # KaHIP imbalance: e.g., 0.03 means 3% imbalance. Map your gamma directly.
        imbalance = float(gamma)

        # KaHIP Python API: edgecut, blocks = kahip.kaffpa(vwgt, xadj, adjcwgt, adjncy, nblocks, imbalance, suppress_output, seed, mode)
        edgecut, blocks = kahip.kaffpa(
            vwgt, xadj, adjcwgt, adjncy,
            int(nparts), imbalance,
            int(self.suppress_output),
            int(self.seed),
            int(self.mode),
        )  # :contentReference[oaicite:2]{index=2}

        part = np.array(blocks, dtype=np.int32)
        if part.shape[0] != m:
            raise RuntimeError(f"KaHIP returned partition length {part.shape[0]} != {m}.")

        return part
