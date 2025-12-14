#!/usr/bin/env python3
"""
Compute statistics for a boolean circuit from a gate file.

This script processes a circuit gate file and outputs statistics including:
- non_free_gates
- max_in_boundary
- max_out_boundary
- max_cross_boundary
- max_load

The script can infer circuit parameters from the file path if it follows the pattern:
  {circuit_name}_u{elem_bits}_N{num_inputs}
For example: bitonic_sort_u32_N16, my_circuit_u64_N128
"""

import os
import sys
import argparse
import re
import numpy as np

# Add src directory to path
script_dir = os.path.dirname(os.path.abspath(__file__))
repo_root = os.path.dirname(os.path.dirname(script_dir))
src_dir = os.path.join(repo_root, 'src')
sys.path.insert(0, src_dir)

from boolean_circuit.graph_synthesizer import CircuitGraph
from boolean_circuit.partitioner import KaHIPPartitioner, BalancedContiguousPartitioner


def infer_circuit_params(gate_file_path):
    """
    Try to infer circuit name, number of inputs, and element bitwidth from file path.
    Expected pattern: {circuit_name}_u{elem_bits}_N{num_inputs}
    Returns (circuit_name, num_inputs, elem_bits) or (None, None, None) if cannot infer.
    """
    path = os.path.abspath(gate_file_path)
    dir_name = os.path.basename(os.path.dirname(path))
    
    # Pattern: {circuit_name}_u{elem_bits}_N{num_inputs}
    # Example: bitonic_sort_u32_N16 -> circuit_name='bitonic_sort', elem_bits=32, num_inputs=16
    match = re.search(r'(.+?)_u(\d+)_N(\d+)', dir_name)
    if match:
        circuit_name = match.group(1)
        elem_bits = int(match.group(2))
        num_inputs = int(match.group(3))
        return circuit_name, num_inputs, elem_bits
    
    # Fallback: try to extract just the number from directory name
    match = re.search(r'N(\d+)', dir_name)
    if match:
        num_inputs = int(match.group(1))
        circuit_name = dir_name
        # Default to 32-bit if not specified
        return circuit_name, num_inputs, 32
    
    # Use directory name as circuit name, but cannot infer inputs/bitwidth
    return dir_name, None, None


def _fmt(value):
    """Format value for output (convert numpy types to int if appropriate)."""
    if isinstance(value, (np.generic,)):
        return int(value)
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


def compute_statistics(gate_file, num_inputs=None, elem_bits=32, nparts=8, 
                      gamma=0.1, seed=2, circuit_name=None, output_file=None):
    """
    Compute circuit statistics from a gate file.
    
    Args:
        gate_file: Path to the gate file (output.gate.txt)
        num_inputs: Number of input elements (if None, will try to infer)
        elem_bits: Element bitwidth (default: 32)
        nparts: Number of partitions (default: 8)
        gamma: KaHIP imbalance tolerance (default: 0.1)
        seed: KaHIP seed (default: 2)
        circuit_name: Circuit name (if None, will try to infer)
        output_file: Output file path (if None, prints to stdout)
    """
    # Validate gate file exists
    if not os.path.exists(gate_file):
        raise FileNotFoundError(f"Gate file not found: {gate_file}")
    
    # Try to infer parameters if not provided
    if circuit_name is None or num_inputs is None:
        inferred_name, inferred_inputs, inferred_bits = infer_circuit_params(gate_file)
        if circuit_name is None:
            circuit_name = inferred_name
        if num_inputs is None:
            num_inputs = inferred_inputs
        if num_inputs is None:
            raise ValueError(
                "Cannot determine number of inputs. "
                "Please provide --num-inputs or ensure gate file is in a directory "
                "with pattern '{circuit_name}_u{elem_bits}_N{num_inputs}'"
            )
        if elem_bits == 32 and inferred_bits is not None:
            elem_bits = inferred_bits
    
    # Default circuit name if still None
    if circuit_name is None:
        circuit_name = os.path.basename(os.path.dirname(gate_file))
    
    # Only print debug info to stderr if not being called from shell script
    # (check if we're being called with env vars, which indicates shell script usage)
    if 'GATE_FILE' in os.environ:
        # Called from shell script, suppress debug output
        pass
    else:
        print(f"Processing circuit: {circuit_name}", file=sys.stderr)
        print(f"  Gate file: {gate_file}", file=sys.stderr)
        print(f"  Inputs: {num_inputs}, Element bits: {elem_bits}, Parts: {nparts}", file=sys.stderr)
    
    # Load circuit graph
    cg = CircuitGraph.from_cbmc_gc_gate_file(gate_file)
    pg = cg.to_partitionable()
    
    # Partition
    partition_note = None
    try:
        partitioner = KaHIPPartitioner(mode=0, seed=seed, suppress_output=1)
        part = partitioner.partition(pg, nparts=nparts, gamma=gamma)
    except Exception as exc:
        partition_note = f"KaHIPPartitioner failed ({exc}); fell back to BalancedContiguousPartitioner."
        # Only print warning to stderr if not being called from shell script
        if 'GATE_FILE' not in os.environ:
            print(f"Warning: {partition_note}", file=sys.stderr)
        fallback = BalancedContiguousPartitioner()
        part = fallback.partition(pg, nparts=nparts, gamma=gamma)
    
    # Compute metrics
    metrics = pg.summary_metrics(part, nparts=nparts)
    
    # Format output
    lines = [
        f"For the circuit '{circuit_name}' the input number is {num_inputs} and each item uses {elem_bits}-bit words.",
        "Circuit statistics:",
        f"  non_free_gates   : {_fmt(metrics['total_nonfree'])}",
        f"  max_in_boundary  : {_fmt(metrics['max_in_boundary'])}",
        f"  max_out_boundary : {_fmt(metrics['max_out_boundary'])}",
        f"  max_cross_boundary: {_fmt(metrics['max_cross_boundary'])}",
        f"  max_load         : {_fmt(metrics['max_load'])}",
    ]
    
    if partition_note:
        lines.append("")
        lines.append(partition_note)
    
    output_text = "\n".join(lines)
    
    # Write output
    # Always print to stdout (for shell script compatibility)
    print(output_text)
    
    # Also write to file if specified
    if output_file:
        with open(output_file, 'w') as f:
            f.write(output_text)
            f.write('\n')
    
    return output_text


def main():
    parser = argparse.ArgumentParser(
        description='Compute statistics for a boolean circuit from a gate file.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s output.gate.txt
  %(prog)s output.gate.txt --num-inputs 16 --output stats.txt
  %(prog)s output.gate.txt -n 32 -p 8 -o stats.txt
  
The script can infer parameters from paths like:
  .../bitonic_sort_u32_N16/output.gate.txt
  .../my_circuit_u64_N128/output.gate.txt

The script also accepts environment variables (used if command-line args not provided):
  GATE_FILE, NUM_PARTS, INPUT_ELEMENTS, ELEMENT_BITWIDTH, CIRCUIT_NAME,
  KAHIP_GAMMA, KAHIP_SEED
        """
    )
    
    parser.add_argument(
        'gate_file',
        nargs='?',
        default=None,
        help='Path to the circuit gate file (output.gate.txt). Can also be set via GATE_FILE env var.'
    )
    
    parser.add_argument(
        '-n', '--num-inputs',
        type=int,
        default=None,
        help='Number of input elements (default: try to infer from path or use INPUT_ELEMENTS env var)'
    )
    
    parser.add_argument(
        '-b', '--element-bits',
        type=int,
        default=None,
        help='Element bitwidth (default: 32 or ELEMENT_BITWIDTH env var)'
    )
    
    parser.add_argument(
        '-p', '--parts',
        type=int,
        default=None,
        help='Number of partitions (default: 8 or NUM_PARTS env var)'
    )
    
    parser.add_argument(
        '-g', '--gamma',
        type=float,
        default=None,
        help='KaHIP imbalance tolerance (default: 0.1 or KAHIP_GAMMA env var)'
    )
    
    parser.add_argument(
        '--seed',
        type=int,
        default=None,
        help='KaHIP seed (default: 2 or KAHIP_SEED env var)'
    )
    
    parser.add_argument(
        '--circuit-name',
        type=str,
        default=None,
        help='Circuit name (default: infer from path or use CIRCUIT_NAME env var)'
    )
    
    parser.add_argument(
        '-o', '--output',
        type=str,
        default=None,
        help='Output file path (default: print to stdout)'
    )
    
    args = parser.parse_args()
    
    # Read from environment variables if command-line args not provided
    gate_file = args.gate_file or os.environ.get('GATE_FILE')
    if not gate_file:
        parser.error('gate_file is required (provide as argument or set GATE_FILE environment variable)')
    
    num_inputs = args.num_inputs
    if num_inputs is None and 'INPUT_ELEMENTS' in os.environ:
        num_inputs = int(os.environ['INPUT_ELEMENTS'])
    
    elem_bits = args.element_bits
    if elem_bits is None:
        if 'ELEMENT_BITWIDTH' in os.environ:
            elem_bits = int(os.environ['ELEMENT_BITWIDTH'])
        else:
            elem_bits = 32
    
    nparts = args.parts
    if nparts is None:
        if 'NUM_PARTS' in os.environ:
            nparts = int(os.environ['NUM_PARTS'])
        else:
            nparts = 8
    
    gamma = args.gamma
    if gamma is None:
        if 'KAHIP_GAMMA' in os.environ:
            gamma = float(os.environ['KAHIP_GAMMA'])
        else:
            gamma = 0.1
    
    seed = args.seed
    if seed is None:
        if 'KAHIP_SEED' in os.environ:
            seed = int(os.environ['KAHIP_SEED'])
        else:
            seed = 2
    
    circuit_name = args.circuit_name
    if circuit_name is None and 'CIRCUIT_NAME' in os.environ:
        circuit_name = os.environ['CIRCUIT_NAME']
    
    try:
        compute_statistics(
            gate_file=gate_file,
            num_inputs=num_inputs,
            elem_bits=elem_bits,
            nparts=nparts,
            gamma=gamma,
            seed=seed,
            circuit_name=circuit_name,
            output_file=args.output
        )
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
