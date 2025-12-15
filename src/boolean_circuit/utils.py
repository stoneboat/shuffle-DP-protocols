import os
import subprocess
import time

def parse_stats_file(path: str):
    """Return (client_comp, client_comm, baseline_comp, baseline_comm) from a stats file."""
    with open(path, "r") as f:
        lines = f.readlines()

    client_comp = None
    client_comm = None
    baseline_comp = None
    baseline_comm = None

    for line in lines:
        if "max_client_balance_computation_cost (s)" in line:
            client_comp = float(line.split(":", 1)[1])
        elif "max_client_balance_communication_cost (MB)" in line:
            client_comm = float(line.split(":", 1)[1])
        elif "baseline computation cost (s)" in line:
            baseline_comp = float(line.split(":", 1)[1])
        elif "baseline communication cost (MB)" in line:
            baseline_comm = float(line.split(":", 1)[1])

    return client_comp, client_comm, baseline_comp, baseline_comm

def compute_circuit_cost(circut_args, dir_args):
    project_dir = dir_args["project_dir"]
    scripts_dir = dir_args["scripts_dir"]
    data_dir = dir_args["data_dir"]
    out_tag = dir_args["out_tag"]

    NPARTS = circut_args["NPARTS"]
    circuit_name = circut_args["circuit_name"]
    

    script = os.path.join(scripts_dir, "boolean_circuit", f"{circuit_name}_cost_estimate.sh")

    # Build output directory under data_dir/boolean_circuits following CIRCUIT_NAME_u{NPARTS}_N{NPARTS}

    out_dir = os.path.join(data_dir, "boolean_circuits", out_tag)
    os.makedirs(os.path.join(data_dir, "boolean_circuits"), exist_ok=True)

    assert circuit_name in ["oblivious_sort", "selection", "tree_mech"], f"Invalid circuit name: {circuit_name}"

    if circuit_name == "oblivious_sort":
        start_time = time.time()
        result = subprocess.run(
            [
                script,
                "-p",
                str(NPARTS),
                "-o",
                out_dir,
            ],
            cwd=project_dir,
            capture_output=True,
            text=True,
            check=False,
        )
        end_time = time.time()
    elif circuit_name == "selection":
        # Default to 16 choices if not provided, matching the script's default
        # Optional parameters for specific circuits
        NUM_CHOICES = circut_args["num_choices"]
        assert NUM_CHOICES is not None, "NUM_CHOICES must be provided for selection"
        start_time = time.time()
        result = subprocess.run(
            [
                script,
                "-p",
                str(NPARTS),
                "-d",
                str(NUM_CHOICES),
                "-o",
                out_dir,
            ],
            cwd=project_dir,
            capture_output=True,
            text=True,
            check=False,
        )
        end_time = time.time()
    elif circuit_name == "tree_mech":
        VEC_DIM = circut_args["vec_dim"]
        assert VEC_DIM is not None, "VEC_DIM must be provided for tree_mech"
        start_time = time.time()
        result = subprocess.run(
            [
                script,
                "-p",
                str(NPARTS),
                "-d",
                str(VEC_DIM),
                "-o",
                out_dir,
            ],
            cwd=project_dir,
            capture_output=True,
            text=True,
            check=False,
        )
        end_time = time.time()
    print(f"Script execution time with NPARTS={NPARTS} and circuit name={circuit_name}: {end_time - start_time} seconds")

    if result.returncode != 0:
        print("Return code:", result.returncode)
        print("STDOUT:\n", result.stdout)
        print("STDERR:\n", result.stderr)
        raise RuntimeError("Circuit cost estimation failed")
    else:
        # Read statistics from the generated stats file
        if circuit_name == "oblivious_sort":
            stats_file = os.path.join(out_dir, "oblivious_sorting_stats.txt")
        elif circuit_name == "selection":
            stats_file = os.path.join(out_dir, "dp_selection_gumbel_stats.txt")
        elif circuit_name == "tree_mech":
            stats_file = os.path.join(out_dir, "tree_mech_stats.txt")

        if not os.path.exists(stats_file):
            raise FileNotFoundError(f"Stats file not found: {stats_file}")
        client_comp, client_comm, baseline_comp, baseline_comm = parse_stats_file(stats_file)
        return client_comp, client_comm, baseline_comp, baseline_comm