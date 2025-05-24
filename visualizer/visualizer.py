import argparse
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import json
import tkinter as tk
from tkinter import messagebox, ttk
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import matplotlib
matplotlib.use('TkAgg')

# Global memory for Load/Store operations
memory = {}

# Parse .dot labels into operation metadata
def infer_op_metadata(data):
    raw_label = data.get('label', '')
    shape = data.get('shape', '')
    lbl = raw_label.strip('"').split('\\n')[0]
    meta = {}
    if lbl.startswith('Const'):
        parts = raw_label.strip('"').split()
        try:
            val = int(parts[-1])
        except ValueError:
            val = 0
        meta['op'] = 'Constant'
        meta['value'] = val
    elif lbl in ('st', 'ld'):
        meta['op'] = 'Store' if lbl == 'st' else 'Load'
    elif '%' in lbl:
        meta['op'] = 'FunctionInput'
        meta['arg_value'] = 0  # Default, overridden by user input
        meta['param_name'] = lbl
    elif lbl == '+':
        meta['op'] = 'BasicBinaryOp'
        meta['op_symbol'] = '+'
    elif lbl == '>':
        meta['op'] = 'BasicBinaryOp'
        meta['op_symbol'] = '>'
    elif lbl == '==':
        meta['op'] = 'BasicBinaryOp'
        meta['op_symbol'] = '=='
    elif lbl == 'M':
        meta['op'] = 'Merge'
    elif lbl == "STR":
        meta['op'] = 'Stream'
    elif lbl == "T":
        meta['op'] = 'TS'
    elif lbl == "F":
        meta['op'] = 'FS'
    elif lbl == 'ret':
        meta['op'] = 'Return'
    else:
        print("Unknown: ", lbl, shape)
        meta['op'] = 'Unknown'
    return meta

# Token-based execution system
class Token:
    def __init__(self, value, source_node=None):
        self.value = value
        self.source_node = source_node
    
    def __repr__(self):
        return f"Token({self.value})"

class TokenBasedExecutor:
    def __init__(self, G):
        self.G = G
        self.node_values = {}  # Current computed values for each node
        self.pending_tokens = {}  # Tokens waiting to be consumed by each node
        self.execution_sequence = []  # Record of execution steps
        self.completed = False
        self.return_value = None
        
        # Initialize pending tokens for each node
        for node in G.nodes():
            self.pending_tokens[node] = []
    
    def reset(self):
        global memory
        memory.clear()
        self.node_values = {}
        self.pending_tokens = {node: [] for node in self.G.nodes()}
        self.execution_sequence = []
        self.completed = False
        self.return_value = None
    
    def add_token(self, node, token):
        """Add a token to a node's input queue"""
        if node in self.pending_tokens:
            self.pending_tokens[node].append(token)
    
    def can_execute(self, node):
        """Check if a node can execute (has all required inputs)"""
        op_type = self.G.nodes[node].get('op', 'Unknown')
        required_inputs = len(list(self.G.predecessors(node)))
        available_tokens = len(self.pending_tokens[node])
        
        # Stream nodes don't need input tokens to start
        if op_type == 'Stream':
            return True
        
        # Constants and FunctionInputs don't need input tokens
        if op_type in ['Constant', 'FunctionInput']:
            return True
        
        # Other nodes need tokens equal to their input edges
        return available_tokens >= required_inputs
    
    def execute_node(self, node):
        """Execute a node and return the output token(s)"""
        op_type = self.G.nodes[node].get('op', 'Unknown')
        input_tokens = self.pending_tokens[node]
        
        result_token = None
        
        if op_type == 'Constant':
            value = self.G.nodes[node].get('value', 0)
            result_token = Token(value, node)
        
        elif op_type == 'FunctionInput':
            value = self.G.nodes[node].get('arg_value', 0)
            result_token = Token(value, node)
        
        elif op_type == 'Stream':
            # Stream node generates an activation token
            result_token = Token(True, node)
        
        elif op_type == 'BasicBinaryOp':
            if len(input_tokens) >= 2:
                a, b = input_tokens[0].value, input_tokens[1].value
                op_symbol = self.G.nodes[node].get('op_symbol', '+')
                
                if op_symbol == '+':
                    result = a + b if isinstance(a, (int, float)) and isinstance(b, (int, float)) else str(a) + str(b)
                elif op_symbol == '>':
                    result = a > b
                elif op_symbol == '==':
                    result = a == b
                else:
                    result = None
                
                if result is not None:
                    result_token = Token(result, node)
        
        elif op_type == 'Load':
            if len(input_tokens) >= 1:
                addr = input_tokens[0].value
                value = memory.get(addr)
                if value is not None:
                    result_token = Token(value, node)
        
        elif op_type == 'Store':
            if len(input_tokens) >= 2:
                addr, val = input_tokens[0].value, input_tokens[1].value
                memory[addr] = val
                result_token = Token(val, node)
        
        elif op_type == 'TS':
            if len(input_tokens) >= 2:
                cond, val = input_tokens[0].value, input_tokens[1].value
                if cond:
                    result_token = Token(val, node)
        
        elif op_type == 'FS':
            if len(input_tokens) >= 2:
                cond, val = input_tokens[0].value, input_tokens[1].value
                if not cond:
                    result_token = Token(val, node)
        
        elif op_type == 'Merge':
            if len(input_tokens) >= 3:
                decider, a, b = input_tokens[0].value, input_tokens[1].value, input_tokens[2].value
                result = a if decider else b
                result_token = Token(result, node)
        
        elif op_type == 'Return':
            if len(input_tokens) >= 1:
                self.return_value = input_tokens[0].value
                self.completed = True
                result_token = Token(input_tokens[0].value, node)
        
        # Clear consumed tokens and store result
        if result_token:
            self.node_values[node] = result_token.value
            
        # Clear the tokens that were consumed
        required_inputs = len(list(self.G.predecessors(node)))
        if op_type in ['Constant', 'FunctionInput', 'Stream']:
            # These don't consume input tokens
            pass
        else:
            # Consume the required number of tokens
            self.pending_tokens[node] = self.pending_tokens[node][required_inputs:]
        
        return result_token
    
    def step(self):
        """Execute one step of the simulation"""    
        if self.completed:
            return None
        
        # Find nodes that can execute
        executable_nodes = [node for node in self.G.nodes() if self.can_execute(node)]
        
        if not executable_nodes:
            return None
        
        # Execute the first executable node
        node = executable_nodes[0]
        result_token = self.execute_node(node)
        
        # Record this execution step
        step_info = {
            'node': node,
            'op_type': self.G.nodes[node].get('op', 'Unknown'),
            'result': result_token.value if result_token else None,
            'completed': self.completed
        }
        self.execution_sequence.append(step_info)
        
        # Propagate the token to successor nodes
        if result_token and not self.completed:
            for successor in self.G.successors(node):
                self.add_token(successor, result_token)
        
        return step_info

# Evaluate a node based on its type and inputs (legacy function for compatibility)
def evaluate_node(G, node, values):
    data = G.nodes[node]
    op_type = data.get('op', 'Unknown')
    inputs = [values[p] for p in G.predecessors(node) if p in values]
    
    if op_type == 'Constant':
        return data.get('value', 0)
    elif op_type == 'FunctionInput':
        return data.get('arg_value', 0)
    elif op_type == 'BasicBinaryOp':
        if len(inputs) != 2:
            return None
        a, b = inputs
        sym = data.get('op_symbol', '+')
        if sym == '+': return a + b if isinstance(a, (int, float)) and isinstance(b, (int, float)) else str(a) + str(b)
        elif sym == '>': return a > b
        elif sym == '==': return a == b
    elif op_type == 'Load':
        if len(inputs) != 1:
            return None
        addr = inputs[0]
        return memory.get(addr)
    elif op_type == 'Store':
        if len(inputs) != 2:
            return None
        addr, val = inputs
        memory[addr] = val
        return val
    elif op_type == 'TS':
        if len(inputs) != 2:
            return None
        cond, val = inputs
        return val if cond else None
    elif op_type == 'FS':
        if len(inputs) != 2:
            return None
        cond, val = inputs
        return val if not cond else None
    elif op_type == 'Merge':
        if len(inputs) != 3:
            return None
        decider, a, b = inputs
        return a if decider else b
    elif op_type == 'Stream':
        return True
    elif op_type == 'Return':
        return inputs[0] if inputs else None
    return None

# Compute values for all nodes in sequence (legacy function)
def compute_values(G):
    executor = TokenBasedExecutor(G)
    executor.reset()
    
    # Keep stepping until completion or no progress
    max_steps = 1000  # Prevent infinite loops
    step_count = 0
    
    while not executor.completed and step_count < max_steps:
        step_info = executor.step()
        if step_info is None:
            break
        step_count += 1
    
    # Convert to legacy format for compatibility
    values = executor.node_values
    sequence = [step['node'] for step in executor.execution_sequence]
    
    return values, sequence, executor

# Read and process the .dot file
def read_graph(dot_path):
    try:
        G = nx.drawing.nx_pydot.read_dot(dot_path)
        if isinstance(G, (nx.MultiDiGraph, nx.MultiGraph)):
            H = nx.DiGraph()
            H.add_nodes_from(G.nodes(data=True))
            for u, v in G.edges():
                H.add_edge(u, v)
            G = H
        for n in G.nodes():
            meta = infer_op_metadata(G.nodes[n])
            for k, v in meta.items():
                G.nodes[n][k] = v
        return G
    except Exception as e:
        print(f"Error reading graph: {e}")
        # Create a simple test graph if file reading fails
        G = nx.DiGraph()
        G.add_node('1', op='Stream')
        G.add_node('2', op='FunctionInput', param_name='%x', arg_value=5)
        G.add_node('3', op='Constant', value=10)
        G.add_node('4', op='BasicBinaryOp', op_symbol='+')
        G.add_node('5', op='Return')
        G.add_edges_from([('1', '4'), ('2', '4'), ('3', '4'), ('4', '5')])
        return G

# Create hierarchical layout for dataflow graphs
def create_hierarchical_layout(G):
    """Create a hierarchical layout where inputs are at the top and outputs at the bottom."""
    if len(G.nodes()) == 0:
        return {}
    
    # Categorize nodes by type
    stream_nodes = []
    input_nodes = []
    return_nodes = []
    intermediate_nodes = []
    
    for node in G.nodes():
        op_type = G.nodes[node].get('op', 'Unknown')
        if op_type == 'Stream':
            stream_nodes.append(node)
        elif op_type in ['FunctionInput', 'Constant']:
            input_nodes.append(node)
        elif op_type == 'Return':
            return_nodes.append(node)
        else:
            intermediate_nodes.append(node)
    
    # If no clear input/output distinction, use topological ordering
    if not stream_nodes and not input_nodes and not return_nodes:
        try:
            topo_order = list(nx.topological_sort(G))
            # Split into layers based on position in topological order
            n_layers = min(4, len(topo_order))  # Maximum 4 layers
            layer_size = len(topo_order) // n_layers
            
            layers = []
            for i in range(n_layers):
                start_idx = i * layer_size
                end_idx = (i + 1) * layer_size if i < n_layers - 1 else len(topo_order)
                layers.append(topo_order[start_idx:end_idx])
        except nx.NetworkXError:  # Graph has cycles
            # Fall back to grouping by in-degree
            layers = [[], [], [], []]  # 4 layers
            for node in G.nodes():
                in_degree = G.in_degree(node)
                layer_idx = min(in_degree, 3)  # Cap at layer 3
                layers[layer_idx].append(node)
    else:
        # Create layers based on node types and distances
        layers = []
        
        # Layer 0: Stream and Input nodes (top)
        top_layer = stream_nodes + input_nodes
        if top_layer:
            layers.append(top_layer)
        
        # Find intermediate layers using shortest path distances
        if intermediate_nodes:
            # Calculate distances from top-level nodes
            distances = {}
            for node in intermediate_nodes:
                min_dist = float('inf')
                for top_node in top_layer:
                    if top_node in G and node in G:
                        try:
                            dist = nx.shortest_path_length(G, top_node, node)
                            min_dist = min(min_dist, dist)
                        except nx.NetworkXNoPath:
                            continue
                
                if min_dist == float('inf'):
                    min_dist = 1  # Default distance if no path found
                distances[node] = min_dist
            
            # Group by distance (create intermediate layers)
            max_dist = max(distances.values()) if distances else 1
            for dist in range(1, max_dist + 1):
                layer_nodes = [node for node, d in distances.items() if d == dist]
                if layer_nodes:
                    layers.append(layer_nodes)
        
        # Last layer: Return nodes (bottom)
        if return_nodes:
            layers.append(return_nodes)
    
    # Position nodes in each layer
    pos = {}
    y_spacing = 2.0  # Vertical spacing between layers
    x_spacing = 2.5  # Horizontal spacing between nodes in same layer
    
    total_layers = len([layer for layer in layers if layer])  # Only count non-empty layers
    
    for layer_idx, layer in enumerate(layers):
        if not layer:  # Skip empty layers
            continue
            
        # Y position (top to bottom)
        y = (total_layers - 1) * y_spacing / 2 - layer_idx * y_spacing
        
        # X positions (centered)
        if len(layer) == 1:
            x_positions = [0]
        else:
            total_width = (len(layer) - 1) * x_spacing
            x_positions = [-total_width/2 + i * x_spacing for i in range(len(layer))]
        
        # Assign positions
        for i, node in enumerate(layer):
            pos[node] = (x_positions[i], y)
    
    return pos

# Enhanced layout with hierarchical option
def create_enhanced_layout(G, layout_type='hierarchical'):
    if layout_type == 'hierarchical':
        return create_hierarchical_layout(G)
    elif layout_type == 'dot': # Added 'dot' option for Graphviz's default layout
        try:
            return nx.drawing.nx_pydot.graphviz_layout(G, prog='dot')
        except ImportError:
            messagebox.showerror("Graphviz Error", "Graphviz and PyGraphviz/Pydot are required for 'dot' layout.\n"
                                                  "Please install Graphviz and 'pip install pygraphviz' (or pydot).")
            return nx.spring_layout(G) # Fallback
    elif layout_type == 'neato': # Example for another Graphviz layout
        try:
            return nx.drawing.nx_pydot.graphviz_layout(G, prog='neato')
        except ImportError:
            messagebox.showerror("Graphviz Error", "Graphviz and PyGraphviz/Pydot are required for 'neato' layout.\n"
                                                  "Please install Graphviz and 'pip install pygraphviz' (or pydot).")
            return nx.spring_layout(G) # FallbackF
    elif layout_type == 'spring':
        pos = nx.spring_layout(G, k=3.0, iterations=100)
    elif layout_type == 'shell':
        pos = nx.shell_layout(G, scale=2.0)
    elif layout_type == 'spectral':
        pos = nx.spectral_layout(G, scale=2.0)
    elif layout_type == 'kamada_kawai':
        pos = nx.kamada_kawai_layout(G, scale=1.0)
    else:
        pos = nx.spring_layout(G, k=3.0, iterations=100)
    
    # Scale positions for more spacing (except hierarchical which is already well-spaced)
    if layout_type not in ['hierarchical', 'dot', 'neato']:
        for node in pos:
            pos[node] = (pos[node][0] * 10.0, pos[node][1] * 5.0)
    
    return pos

class DataflowSimulator:
    def __init__(self, root, G, layout):
        self.root = root
        self.root.title("Token-Based Dataflow Simulation")
        self.root.geometry("1400x1000")  # Increased height
        self.root.configure(bg='#f0f0f0')
        
        self.G = G.copy()
        self.layout = layout
        self.current_step = 0
        self.input_widgets = {}
        self.executor = TokenBasedExecutor(self.G)
        
        # Initialize input values
        self.input_values = {}
        for node in self.G.nodes():
            if self.G.nodes[node].get('op') == 'FunctionInput':
                self.input_values[node] = self.G.nodes[node].get('arg_value', 0)
        
        self.create_widgets()
        self.reset_simulation()

    def create_widgets(self):
        # Main container with padding
        main_frame = tk.Frame(self.root, bg='#f0f0f0')
        main_frame.pack(fill='both', expand=True, padx=10, pady=10)
        
        # Graph frame - takes up most of the space but leaves room for controls
        graph_frame = tk.Frame(main_frame, bg='white', relief='sunken', bd=2)
        graph_frame.pack(fill='both', expand=True, side='top')
        
        # Create matplotlib figure with adjusted size
        self.fig, self.ax = plt.subplots(figsize=(14, 8))
        self.fig.patch.set_facecolor('white')
        
        # Create canvas and pack it
        self.canvas = FigureCanvasTkAgg(self.fig, master=graph_frame)
        self.canvas.get_tk_widget().pack(fill='both', expand=True, padx=5, pady=5)
        
        # Control panel - fixed height at bottom
        control_frame = tk.Frame(main_frame, bg='#e0e0e0', relief='raised', bd=2, height=180)
        control_frame.pack(fill='x', side='bottom', pady=(10, 0))
        control_frame.pack_propagate(False)  # Maintain fixed height
        
        # Inner container for better organization
        control_inner = tk.Frame(control_frame, bg='#e0e0e0')
        control_inner.pack(fill='both', expand=True, padx=5, pady=5)
        
        # Left side - Input controls
        input_frame = tk.LabelFrame(control_inner, text="Function Inputs", 
                                  font=("Arial", 11, "bold"), bg='#e0e0e0', fg='#333')
        input_frame.pack(side='left', fill='both', expand=True, padx=(0, 10))
        
        # Create scrollable frame for inputs if there are many
        input_canvas = tk.Canvas(input_frame, bg='#e0e0e0', highlightthickness=0)
        input_scrollbar = ttk.Scrollbar(input_frame, orient="vertical", command=input_canvas.yview)
        scrollable_input_frame = tk.Frame(input_canvas, bg='#e0e0e0')
        
        scrollable_input_frame.bind(
            "<Configure>",
            lambda e: input_canvas.configure(scrollregion=input_canvas.bbox("all"))
        )
        
        input_canvas.create_window((0, 0), window=scrollable_input_frame, anchor="nw")
        input_canvas.configure(yscrollcommand=input_scrollbar.set)
        
        # Create input widgets
        input_nodes = [(n, self.G.nodes[n]) for n in self.G.nodes() 
                      if self.G.nodes[n].get('op') == 'FunctionInput']
        
        if input_nodes:
            for i, (node_id, node_data) in enumerate(input_nodes):
                row_frame = tk.Frame(scrollable_input_frame, bg='#e0e0e0')
                row_frame.pack(fill='x', padx=10, pady=3)
                
                param_name = node_data.get('param_name', f'Node {node_id}').strip('"')
                tk.Label(row_frame, text=f"{param_name}:", font=("Arial", 10), 
                        bg='#e0e0e0', width=12, anchor='e').pack(side='left')
                
                var = tk.StringVar(value=str(self.input_values[node_id]))
                entry = tk.Entry(row_frame, textvariable=var, font=("Arial", 10), width=8)
                entry.pack(side='left', padx=(5, 0))
                
                self.input_widgets[node_id] = var
                var.trace('w', lambda *args, nid=node_id: self.on_input_change(nid))
            
            # Only show scrollbar if needed
            if len(input_nodes) > 4:
                input_canvas.pack(side="left", fill="both", expand=True)
                input_scrollbar.pack(side="right", fill="y")
            else:
                input_canvas.pack(side="left", fill="both", expand=True)
        else:
            tk.Label(input_frame, text="No function inputs found", 
                    font=("Arial", 10), bg='#e0e0e0').pack(padx=10, pady=20)
        
        # Right side - Controls with fixed width
        control_right = tk.LabelFrame(control_inner, text="Simulation Control", 
                                    font=("Arial", 11, "bold"), bg='#e0e0e0', fg='#333', width=200)
        control_right.pack(side='right', fill='y', padx=(10, 0))
        control_right.pack_propagate(False)  # Maintain fixed width
        
        # Center the controls vertically
        center_frame = tk.Frame(control_right, bg='#e0e0e0')
        center_frame.pack(expand=True)
        
        # Buttons
        self.step_button = tk.Button(center_frame, text="Next Step", command=self.next_step,
                                   font=("Arial", 12, "bold"), bg='#4CAF50', fg='white',
                                   padx=20, pady=8, relief='raised', bd=3, width=12)
        self.step_button.pack(pady=(10, 5))
        
        self.reset_button = tk.Button(center_frame, text="Reset", command=self.reset_simulation,
                                    font=("Arial", 10), bg='#f44336', fg='white',
                                    padx=15, pady=5, relief='raised', bd=2, width=12)
        self.reset_button.pack(pady=5)
        
        # Status
        self.status_label = tk.Label(center_frame, text="Step: 0", 
                                   font=("Arial", 11, "bold"), bg='#e0e0e0')
        self.status_label.pack(pady=(10, 10))

    def on_input_change(self, node_id):
        try:
            value = int(self.input_widgets[node_id].get().strip())
        except ValueError:
            value = 0
        
        self.input_values[node_id] = value
        self.G.nodes[node_id]['arg_value'] = value
        self.reset_simulation()

    def reset_simulation(self):
        global memory
        memory.clear()
        self.current_step = 0
        
        # Update graph with current input values
        for node_id, value in self.input_values.items():
            if node_id in self.G.nodes:
                self.G.nodes[node_id]['arg_value'] = value
        
        # Reset executor
        self.executor = TokenBasedExecutor(self.G)
        self.executor.reset()
        
        # Update UI
        self.step_button.config(text="Next Step", state='normal')
        self.status_label.config(text="Step: 0")
        self.update_plot()

    def next_step(self):
        if not self.executor.completed:
            step_info = self.executor.step()
            if step_info:
                self.current_step += 1
                self.update_plot()
                self.status_label.config(text=f"Step: {self.current_step}")
                
                if self.executor.completed:
                    self.step_button.config(text=f"Completed! Return: {self.executor.return_value}", state='disabled')
            else:
                self.step_button.config(text="No Progress Possible", state='disabled')

    def update_plot(self):
        self.ax.clear()
        
        if not self.G.nodes():
            self.ax.text(0.5, 0.5, 'No graph data available', 
                        ha='center', va='center', transform=self.ax.transAxes, fontsize=16)
            self.canvas.draw()
            return
        
        # Colors and sizes based on node types and execution status
        executed_nodes = set(step['node'] for step in self.executor.execution_sequence)
        colors = []
        sizes = []
        
        for n in self.G.nodes():
            op_type = self.G.nodes[n].get('op', 'Unknown')
            
            # Color based on type and execution status
            if n in executed_nodes:
                if op_type == 'Stream':
                    colors.append('lightcoral')
                elif op_type == 'FunctionInput':
                    colors.append('lightgreen')
                elif op_type == 'Constant':
                    colors.append('lightcyan')
                elif op_type == 'Return':
                    colors.append('gold')
                else:
                    colors.append('lightgreen')
            else:
                if op_type == 'Stream':
                    colors.append('salmon')
                elif op_type == 'FunctionInput':
                    colors.append('lightblue')
                elif op_type == 'Constant':
                    colors.append('lightsteelblue')
                elif op_type == 'Return':
                    colors.append('wheat')
                else:
                    colors.append('lightgray')
            
            # Size based on execution status
            sizes.append(1700 if n in executed_nodes else 1200)
        
        # Draw graph
        pos = self.layout
        
        # Edges with better styling
        if self.G.edges():
            nx.draw_networkx_edges(self.G, pos, ax=self.ax, arrowstyle='->',
                node_size=sizes, arrowsize=25, edge_color='darkgray',
                width=1.5, alpha=.7,
                connectionstyle='arc3,rad=0.3') # Added connectionstyle
        
        # Nodes
        nx.draw_networkx_nodes(self.G, pos, node_color=colors, node_size=sizes,
                             ax=self.ax, linewidths=2, edgecolors='black', alpha=0.9)
        
        # Labels with better formatting
        labels = {}
        for n in self.G.nodes():
            op_type = self.G.nodes[n].get('op', 'Unknown')
            param_name = self.G.nodes[n].get('param_name', '').strip('"')
            value = self.executor.node_values.get(n, '')
            
            if op_type == 'FunctionInput':
                display_name = param_name if param_name else f'Input{n}'
                labels[n] = f"{display_name}\n= {value}" if value != '' else display_name
            elif op_type == 'Constant':
                labels[n] = f"Const\n{value}" if value != '' else "Const"
            elif op_type == 'Stream':
                labels[n] = f"STR\n= {value}" if value != '' else "STR"
            elif op_type == 'Return':
                labels[n] = f"ret\n= {value}" if value != '' else "ret"
            elif op_type == 'BasicBinaryOp':
                symbol = self.G.nodes[n].get('op_symbol', '?')
                labels[n] = f"{symbol}\n= {value}" if value != '' else symbol
            else:
                labels[n] = f"{op_type}\n= {value}" if value != '' else op_type
        
        nx.draw_networkx_labels(self.G, pos, labels, font_size=10, ax=self.ax, 
                              font_weight='bold', verticalalignment='center')
        
        # Title
        if self.current_step == 0:
            title = "Ready to Start - Activation flows from STR to ret nodes"
        elif not self.executor.completed:
            if self.executor.execution_sequence:
                last_step = self.executor.execution_sequence[-1]
                last_node = last_step['node']
                last_value = last_step['result']
                title = f"Step {self.current_step}: Executed Node {last_node} = {last_value}"
            else:
                title = f"Step {self.current_step}"
        else:
            title = f"Simulation Complete! Return Value: {self.executor.return_value}"
        
        self.ax.set_title(title, fontsize=14, fontweight='bold', pad=20)
        
        # Memory display
        memory_text = f"Memory: {dict(memory)}" if memory else "Memory: {}"
        self.ax.text(0.02, 0.98, memory_text, transform=self.ax.transAxes, 
                    fontsize=10, verticalalignment='top',
                    bbox=dict(boxstyle="round,pad=0.3", facecolor="yellow", alpha=0.7))
        
        self.ax.axis('off')
        self.canvas.draw()

def main():
    parser = argparse.ArgumentParser(description="Token-Based Dataflow Graph Simulator")
    parser.add_argument('--layout', choices=['hierarchical', 'spring', 'shell', 'spectral', 'kamada_kawai', 'dot', 'neato'], 
                        default='dot', help="Layout algorithm (default: hierarchical)")
    parser.add_argument('--interval', type=int, default=1000, help="Animation interval in ms")
    parser.add_argument('--dot', default='dfg.dot', help="Path to .dot file")
    parser.add_argument('--inputs', type=str, help='JSON string of inputs (optional)')
    args = parser.parse_args()

    # Read graph
    G = read_graph(args.dot)
    layout = create_enhanced_layout(G, args.layout)

    # Apply input arguments if provided
    if args.inputs:
        try:
            input_values = json.loads(args.inputs)
            for node, value in input_values.items():
                if node in G.nodes and G.nodes[node].get('op') == 'FunctionInput':
                    G.nodes[node]['arg_value'] = value
        except json.JSONDecodeError:
            print("Error: Invalid JSON input format.")

    # Create and run GUI
    root = tk.Tk()
    app = DataflowSimulator(root, G, layout)
    root.mainloop()

if __name__ == '__main__':
    main()