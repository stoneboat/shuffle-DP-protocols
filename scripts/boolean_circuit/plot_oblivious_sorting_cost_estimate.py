#!/usr/bin/env python3
"""
Plot computation and communication cost for oblivious sorting
as a function of the number of clients (NPARTS).

This script wraps the notebook logic in a reusable CLI tool.

Example:
  python scripts/boolean_circuit/plot_oblivious_sorting_cost_estimate.py --nparts 32 64 128 256
"""

import argparse
import os
import sys
from concurrent.futures import ProcessPoolExecutor
from typing import Iterable, List, Tuple

import matplotlib

# Use a non-interactive backend so the script works in headless environments
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402


def _project_paths() -> Tuple[str, str, str, str]:
    """
    Return (project_dir, src_dir, fig_dir, data_dir).

    - project_dir: repository root
    - src_dir: Python source directory to put on sys.path
    - fig_dir: default figure output directory
    - data_dir: default build directory for circuit artifacts
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.abspath(os.path.join(script_dir, "..", ".."))
    src_dir = os.path.join(project_dir, "src")
    fig_dir = os.path.join(project_dir, "fig")
    data_dir = os.path.join(project_dir, "build")
    return project_dir, src_dir, fig_dir, data_dir


def _run_one(args: Tuple[int, str, str, str, str]) -> Tuple[float, float, float, float]:
    """
    Worker function for parallel cost computation.

    Runs in a separate process when using ProcessPoolExecutor.
    """
    nparts, circuit_name, project_dir, scripts_dir, data_dir = args

    # Import here so each process can resolve the module independently
    from boolean_circuit.utils import compute_circuit_cost

    # Debug info to confirm multiprocessing vs threading
    print(f"[worker] nparts={nparts}, python_pid={os.getpid()}", flush=True)

    out_tag = f"{circuit_name}_u32_N{nparts}"
    return compute_circuit_cost(
        nparts, circuit_name, project_dir, scripts_dir, data_dir, out_tag
    )


def plot_oblivious_sort_costs(
    nparts_list: Iterable[int],
    project_dir: str,
    scripts_dir: str,
    data_dir: str,
    fig_dir: str,
    circuit_name: str = "oblivious_sort",
):
    """
    Compute cost stats for each NPARTS and save a comparison figure.

    Returns (fig, out_path, results) where results is a dict with arrays.
    """
    nparts_list = list(nparts_list)

    client_comp_vals: List[float] = []
    baseline_comp_vals: List[float] = []
    client_comm_vals: List[float] = []
    baseline_comm_vals: List[float] = []

    # Run cost estimation in parallel across NPARTS using processes.
    worker_args = [
        (nparts, circuit_name, project_dir, scripts_dir, data_dir)
        for nparts in nparts_list
    ]

    max_workers = min(4, len(nparts_list)) or 1
    with ProcessPoolExecutor(max_workers=max_workers) as executor:
        results = list(executor.map(_run_one, worker_args))

    for client_comp, client_comm, baseline_comp, baseline_comm in results:
        client_comp_vals.append(client_comp)
        baseline_comp_vals.append(baseline_comp)
        client_comm_vals.append(client_comm)
        baseline_comm_vals.append(baseline_comm)

    # Plot computation and communication vs NPARTS
    fig, axes = plt.subplots(1, 2, figsize=(10, 4), sharex=True)

    # Use log2(NPARTS) as the x-coordinate so spacing is uniform
    x_vals = [np.log2(n) for n in nparts_list]

    ax0, ax1 = axes
    ax0.plot(x_vals, client_comp_vals, "o-", label="Ours")
    ax0.plot(x_vals, baseline_comp_vals, "s-", label="HIKR23")
    xticks = x_vals
    ax0.set_xticks(xticks)
    ax0.set_xticklabels([int(v) for v in xticks])
    ax0.set_xlabel("log2(Number of clients)")
    ax0.set_ylabel("Computation (s)")
    ax0.set_title("Computation cost")
    ax0.legend()

    ax1.plot(x_vals, client_comm_vals, "o-", label="Ours")
    ax1.plot(x_vals, baseline_comm_vals, "s-", label="HIKR23")
    ax1.set_xticks(xticks)
    ax1.set_xticklabels([int(v) for v in xticks])
    ax1.set_xlabel("log2(Number of clients)")
    ax1.set_ylabel("Bandwidth (MB)")
    ax1.set_title("Communication cost")
    ax1.legend()

    # Joint caption under both subplots
    caption = (
        "Client-side computation and communication cost for the Distinct Elements problem"
    )
    # Place caption slightly inside the figure and leave space at bottom
    fig.text(0.5, 0.02, caption, ha="center")

    fig.tight_layout(rect=[0, 0.06, 1, 1])

    os.makedirs(fig_dir, exist_ok=True)
    out_path = os.path.join(fig_dir, f"{circuit_name}_cost_vs_nparts.png")
    # Use bbox_inches='tight' to ensure caption text is included in the saved figure
    fig.savefig(out_path, bbox_inches="tight")
    print(f"Saved figure to {out_path}")

    results = {
        "nparts": nparts_list,
        "client_comp": client_comp_vals,
        "baseline_comp": baseline_comp_vals,
        "client_comm": client_comm_vals,
        "baseline_comm": baseline_comm_vals,
    }

    return fig, out_path, results


def main(argv: Iterable[str] | None = None) -> int:
    project_dir, src_dir, fig_dir_default, data_dir_default = _project_paths()
    scripts_dir = os.path.join(project_dir, "scripts")

    parser = argparse.ArgumentParser(
        description="Plot oblivious sorting cost vs number of clients (NPARTS)."
    )
    parser.add_argument(
        "--nparts",
        nargs="+",
        type=int,
        default=[32, 64, 128, 256],
        help="List of NPARTS values to evaluate (default: 32 64 128 256).",
    )
    parser.add_argument(
        "--data-dir",
        type=str,
        default=data_dir_default,
        help=f"Directory containing build/boolean_circuits artifacts "
        f"(default: {data_dir_default!r}).",
    )
    parser.add_argument(
        "--fig-dir",
        type=str,
        default=fig_dir_default,
        help=f"Directory to write figures (default: {fig_dir_default!r}).",
    )
    parser.add_argument(
        "--circuit-name",
        type=str,
        default="oblivious_sort",
        help="Base circuit name (default: oblivious_sort).",
    )

    args = parser.parse_args(list(argv) if argv is not None else None)

    # Ensure src is importable
    if src_dir not in sys.path:
        sys.path.insert(0, src_dir)

    plot_oblivious_sort_costs(
        nparts_list=args.nparts,
        project_dir=project_dir,
        scripts_dir=scripts_dir,
        data_dir=args.data_dir,
        fig_dir=args.fig_dir,
        circuit_name=args.circuit_name,
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())


