import argparse
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

# Example memory for Load/Store simulation
memory = {}

# Helper to evaluate a single node based on its type and inputs
def evaluate_node(G, node, values):
    data = G.nodes[node]
    op_type = data.get('op', 'Unknown')
    # Get input values
    inputs = [values[p] for p in G.predecessors(node) if p in values]
    # Determine result
    if op_type == 'Constant':
        return float(data.get('value', 0))
    elif op_type == 'FunctionInput':
        return float(data.get('arg_value', 0))
    elif op_type == 'BasicBinaryOp':
        a, b = inputs
        symbol = data.get('op_symbol', '+')
        return {
            '+': a + b,
            '-': a - b,
            '*': a * b,
            '/': a / b if b != 0 else float('inf'),
            '&': int(a) & int(b),
            '|': int(a) | int(b),
            '^': int(a) ^ int(b)
        }[symbol]
    elif op_type == 'Load':
        addr = inputs[0]
        return memory.get(addr, None)
    elif op_type == 'Store':
        addr, val = inputs
        memory[addr] = val
        return val
    elif op_type in ('TrueSteer', 'FalseSteer'):
        cond, val = inputs
        return val if (op_type == 'TrueSteer' and cond) or (op_type == 'FalseSteer' and not cond) else None
    elif op_type == 'Merge':
        decider, a, b = inputs
        return a if decider else b
    elif op_type == 'Carry':
        return inputs[-1] if inputs else None
    elif op_type == 'Invariant':
        return inputs[0] if inputs else None
    elif op_type == 'Order':
        return inputs[1] if len(inputs) > 1 else None
    elif op_type == 'Stream':
        return inputs[0] if inputs else None
    elif op_type == 'FunctionOutput':
        return inputs[0] if inputs else None
    else:
        return None

# Compute values in topological or sequence order
def compute_values(G, sequence):
    values = {}
    for node in sequence:
        val = evaluate_node(G, node, values)
        values[node] = val
        data = G.nodes[node]
        symbol = data.get('op_symbol')
        display = symbol if symbol else data.get('op', 'Unknown')
        print(f"Node {node} [{display}]: {val}")
    return values

# Read and prepare graph, setting labels and preserving op
def read_graph(dot_path):
    try:
        G = nx.drawing.nx_pydot.read_dot(dot_path)
    except ImportError:
        raise ImportError("pydot or pygraphviz is required. Install via 'pip install pydot networkx'")
    # Flatten multigraphs
    if isinstance(G, nx.MultiDiGraph) or isinstance(G, nx.MultiGraph):
        H = nx.DiGraph()
        for u, v, data in G.edges(data=True):
            H.add_edge(u, v)
        G = H
    # Ensure op attribute exists and set label
    for n in G.nodes():
        data = G.nodes[n]
        op_type = data.get('op', 'Unknown')
        data['op'] = op_type
        symbol = data.get('op_symbol')
        if symbol:
            label = f"{n}\n({symbol})"
        elif op_type == 'Constant':
            val = data.get('value', '')
            label = f"{n}\n(Const={val})"
        else:
            label = f"{n}\n({op_type})"
        data['label'] = label
    return G

# Sequence nodes for processing
def compute_sequence(G):
    if nx.is_directed_acyclic_graph(G):
        return list(nx.topological_sort(G))
    seq = []
    for source in [n for n, d in G.in_degree() if d == 0]:
        for node in nx.bfs_tree(G, source):
            seq.append(node)
    for n in G.nodes():
        if n not in seq:
            seq.append(n)
    return seq

# Animate flow with larger nodes and clear labels
def animate_flow(G, sequence, layout, interval=1000):
    fig, ax = plt.subplots(figsize=(10, 8))
    pos = layout
    nodes = list(G.nodes())
    values = compute_values(G, sequence)

    def update(frame):
        ax.clear()
        idx = frame
        colors = ['skyblue' if i <= idx else 'lightgray' for i in range(len(nodes))]
        sizes = [800 if i <= idx else 400 for i in range(len(nodes))]
        nx.draw_networkx_edges(G, pos, ax=ax, arrowstyle='->', arrowsize=12, edge_color='gray')
        nx.draw_networkx_nodes(G, pos, node_color=colors, node_size=sizes, ax=ax, linewidths=1.5, edgecolors='black')
        labels = {n: f"{G.nodes[n]['label']}\n{values.get(n, '')}" for n in nodes}
        nx.draw_networkx_labels(G, pos, labels, font_size=10, ax=ax)
        ax.set_title(f"Step {idx+1}/{len(sequence)}: Node {sequence[idx]} = {values.get(sequence[idx])}")
        ax.axis('off')

    ani = FuncAnimation(fig, update, frames=len(sequence), interval=interval, repeat=False)
    plt.tight_layout()
    plt.show()

# Main entry
def main():
    parser = argparse.ArgumentParser(description="Visualize dataflow with clear node labels and values.")
    parser.add_argument('--layout', choices=['spring', 'shell', 'spectral', 'kamada_kawai'], default='spring')
    parser.add_argument('--interval', type=int, default=1000)
    parser.add_argument('--dot', default='dfg.dot')
    args = parser.parse_args()

    G = read_graph(args.dot)
    layout = getattr(nx, f"{args.layout}_layout")(G)
    seq = compute_sequence(G)
    animate_flow(G, seq, layout, interval=args.interval)

if __name__ == '__main__':
    main()
