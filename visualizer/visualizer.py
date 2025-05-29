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
            try:
                val = float(parts[-1]) # Allow float constants
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
        meta['op'] = 'TS' # True Steer
    elif lbl == "F":
        meta['op'] = 'FS' # False Steer
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
        self.node_values = {}  # Current computed values for each node (output of the node)
        self.pending_tokens = {}  # Tokens waiting to be consumed by each node's inputs
        self.execution_sequence = []  # Record of execution steps
        self.completed = False
        self.return_value = None
        
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

    def get_op_arity(self, node_id):
        """Gets the number of input tokens an operation consumes."""
        op_type = self.G.nodes[node_id].get('op', 'Unknown')
        if op_type in ['Constant', 'FunctionInput', 'Stream']:
            return 0  # These are source nodes, produce output without consuming input tokens
        elif op_type == 'Load':
            return 2
        elif op_type in ['BasicBinaryOp', 'TS', 'FS']:
            return 2
        elif op_type in ['Merge', 'Store']:
            # Standard merge takes a decider and two values
            # Graph must ensure predecessors are ordered: decider, true_val, false_val
            return 3
        elif op_type == 'Return':
            return len(list(self.G.predecessors(node_id)))
        else: # Unknown or other types
            # Fallback: require tokens from all connected predecessors.
            # This might not be correct for all custom nodes.
            return len(list(self.G.predecessors(node_id)))

    def add_token(self, node, token):
        if node in self.pending_tokens:
            self.pending_tokens[node].append(token)
    
    def can_execute(self, node):
        op_type = self.G.nodes[node].get('op', 'Unknown')
        
        ## OLD
        # Source nodes can execute if they haven't produced their value yet in this run
        #if op_type in ['Constant', 'FunctionInput', 'Stream']:
        #    return node not in self.node_values # Has this node already produced an output?
                                                # Resetting simulation clears node_values.
        ## NEW
        # Source nodes can execute every step
        if op_type in ['Constant', 'FunctionInput', 'Stream']:
            return True 

        # For other nodes, check if enough tokens are available for their arity
        required_inputs = self.get_op_arity(node)
        available_tokens = len(self.pending_tokens[node])
        
        return available_tokens >= required_inputs
    
    def execute_node(self, node):
        op_type = self.G.nodes[node].get('op', 'Unknown')
        
        # input_tokens are those queued for this node.
        # We will consume 'arity' number of these for operations that have inputs.
        current_input_tokens = self.pending_tokens[node] 
        
        result_token = None
        consumed_count = 0

        arity = self.get_op_arity(node)
        
        if op_type == 'Constant':
            value = self.G.nodes[node].get('value', 0)
            result_token = Token(value, node)
        
        elif op_type == 'FunctionInput':
            value = self.G.nodes[node].get('arg_value', 0)
            result_token = Token(value, node)
        
        elif op_type == 'Stream':
            result_token = Token(True, node) # Stream node generates an activation token
        
        elif op_type == 'BasicBinaryOp':
            # Assumes get_op_arity returned 2 and can_execute confirmed enough tokens
            a_val = current_input_tokens[0].value
            b_val = current_input_tokens[1].value
            consumed_count = 2
            op_symbol = self.G.nodes[node].get('op_symbol', '+')
            
            # Handle boolean inputs by converting to int (True=1, False=0) for arithmetic
            # This allows stream tokens to participate in simple arithmetic if intended
            if isinstance(a_val, bool): a_val = int(a_val)
            if isinstance(b_val, bool): b_val = int(b_val)

            if op_symbol == '+':
                # If both are numbers (int or float), add them. Otherwise, concatenate as strings.
                if isinstance(a_val, (int, float)) and isinstance(b_val, (int, float)):
                    result = a_val + b_val
                else:
                    result = str(a_val) + str(b_val)
            elif op_symbol == '-':
                # Attempt numeric comparison first, fallback to string if types mismatch for numeric
                try:
                    result = a_val - b_val
                except:
                    result = str(a_val) - str(b_val)
            elif op_symbol == '<<':
                # Attempt numeric comparison first, fallback to string if types mismatch for numeric
                try: 
                    result = a_val << b_val
                except:
                    result = str(a_val) << str(b_val)
            elif op_symbol == '>>':
                # Attempt numeric comparison first, fallback to string if types mismatch for numeric
                try:
                    result = a_val >> b_val
                except:
                    result = str(a_val) >> str(b_val)
            elif op_symbol == '>':
                # Attempt numeric comparison first, fallback to string if types mismatch for numeric
                try:
                    result = a_val > b_val
                except TypeError: # e.g. comparing int and string
                    result = str(a_val) > str(b_val)
            elif op_symbol == '<':
                # Attempt numeric comparison first, fallback to string if types mismatch for numeric
                try:
                    result = a_val < b_val
                except TypeError: # e.g. comparing int and string
                    result = str(a_val) < str(b_val)
            elif op_symbol == '==':
                result = a_val == b_val # Handles mixed types appropriately
            elif op_symbol == '!=':
                result = not (a_val == b_val) # Handles mixed types appropriately
            else:
                result = None # Unknown binary operation symbol
            
            if result is not None:
                result_token = Token(result, node)
        
        elif op_type == 'Load':
            # Assumes arity 2
            addr = current_input_tokens[0].value
            offset = current_input_tokens[1].value
            consumed_count = 2
            value = memory.get(addr + offset) # Returns None if addr not in memory
            if value is not None:
                result_token = Token(value, node)
            # If value is None (address not found), no token is propagated.
            # This means downstream nodes waiting for this load will not activate.
        
        elif op_type == 'Store':
            # Assumes arity 3: value, addr, offset_token
            # IMPORTANT: Assumes predecessors in .dot file are ordered: 1st for address, 2nd for value, 3rd for offset
            offset = current_input_tokens[0].value
            addr = current_input_tokens[1].value
            val = current_input_tokens[2].value
            memory[addr+offset] = val
            result_token = Token(val, node) # Store op often outputs the stored value
            consumed_count = 3
        
        elif op_type == 'TS': # True Steer
            # Assumes arity 2: condition_token, value_token
            cond = current_input_tokens[0].value
            val = current_input_tokens[1].value
            consumed_count = 2
            if cond: # Truthy check
                result_token = Token(val, node)
        
        elif op_type == 'FS': # False Steer
            # Assumes arity 2: condition_token, value_token
            cond = current_input_tokens[0].value
            val = current_input_tokens[1].value
            consumed_count = 2
            if not cond: # Falsy check
                result_token = Token(val, node)
        
        elif op_type == 'Merge':
            # Assumes arity 3: decider_token, true_value_token, false_value_token
            # IMPORTANT: Assumes predecessors in .dot file are ordered correctly.
            decider = current_input_tokens[0].value
            true_val = current_input_tokens[1].value
            false_val = current_input_tokens[2].value
            consumed_count = 3
            result = true_val if decider else false_val
            result_token = Token(result, node)
        
        elif op_type == 'Return':
            # Assumes arity 1
            if current_input_tokens: # Check if there's at least one token
                self.return_value = current_input_tokens[0].value
                self.completed = True
                result_token = Token(self.return_value, node)
                consumed_count = 1

        if result_token:
            self.node_values[node] = result_token.value
            
        if consumed_count > 0:
            self.pending_tokens[node] = self.pending_tokens[node][consumed_count:]
        
        return result_token
    
    def step(self):
        if self.completed:
            return None
        
        executable_nodes = [node for node in self.G.nodes() if self.can_execute(node)]
        
        if not executable_nodes:
            # Try to find any pending tokens to see if graph is stuck
            stuck = any(self.pending_tokens[n] for n in self.pending_tokens)
            if stuck and not self.completed:
                # print("Warning: No node can execute, but pending tokens exist. Graph might be stuck or require more inputs.")
                pass # Keep this silent for now, GUI will show "No Progress"
            return None
        
        # Simple execution order: all in the list.
        # More complex schedulers could be implemented here.
        result_tokens = []
        for node_to_execute in executable_nodes:
            result_tokens.append(self.execute_node(node_to_execute))

        
        step_info = {
            'nodes': executable_nodes,
            'results': [rt.value for rt in result_tokens if rt is not None] if result_tokens else None,
            'completed': self.completed,
            'source_nodes_for_tokens': [rt.source_node for rt in result_tokens if rt is not None] if result_tokens else None,
        }
        self.execution_sequence.append(step_info)
        
        for result_token in result_tokens:
            if result_token and not self.completed:
                for node_to_execute in executable_nodes:
                    if (result_token.source_node == node_to_execute):
                        for successor in self.G.successors(node_to_execute):
                            self.add_token(successor, Token(result_token.value, node_to_execute)) # Pass copies of token value
        
        return step_info


# Read and process the .dot file
def read_graph(dot_path):
    try:
        G_raw = nx.drawing.nx_pydot.read_dot(dot_path)
        # Ensure G is a DiGraph, not MultiDiGraph, for simplicity in this executor.
        # If multiple edges exist between two nodes from pydot, this takes one.
        # For dataflow, usually one functional edge is intended.
        G = nx.DiGraph()
        if G_raw.nodes(): #Check if graph is not empty
            G.add_nodes_from(G_raw.nodes(data=True))
            # For edges, ensure they are added with any attributes from the .dot file
            # though this example doesn't explicitly use edge attributes for execution logic.
            G.add_edges_from(G_raw.edges(data=True))

        for n in G.nodes():
            # pydot might store attributes in a 'value' sub-dictionary or directly
            node_attrs = G.nodes[n]
            # If 'label' is not directly an attribute, pydot might place it inside 'value'
            # This can happen if the .dot node is defined like: node1 [label="val", value="other_val"]
            # However, standard .dot for graphviz usually means label is the primary display.
            # The provided infer_op_metadata expects 'label' and 'shape' at the top level of node_attrs.
            meta = infer_op_metadata(node_attrs)
            for k, v in meta.items():
                G.nodes[n][k] = v
        return G
    except Exception as e:
        print(f"Error reading graph: {e}")
        messagebox.showerror("Graph Read Error", f"Could not read or parse .dot file: {dot_path}\n{e}\nLoading a default example graph.")
        G = nx.DiGraph()
        G.add_node('source1', op='Stream', label='STR')
        G.add_node('val_A', op='FunctionInput', param_name='%A', arg_value=5, label='%A')
        G.add_node('val_B', op='Constant', value=10, label='Const\n10')
        G.add_node('add_op', op='BasicBinaryOp', op_symbol='+', label='+')
        G.add_node('cond_op', op='BasicBinaryOp', op_symbol='>', label='>') # e.g. A > B
        G.add_node('ts_op', op='TS', label='T') # True select
        G.add_node('const_true_path', op='Constant', value=100, label='Const\n100')
        G.add_node('return_node', op='Return', label='ret')
        
        # Edges for add_op: source1 (control), val_A, val_B
        # Let's assume add_op only uses val_A and val_B, stream is for broader flow control
        # If '+' takes 2 inputs, Stream token might be consumed if not handled by arity
        # With current arity logic, BasicBinaryOp '+' will take 2 tokens.
        # For this example, let's make it simpler: A+B
        G.add_edge('val_A', 'add_op')
        G.add_edge('val_B', 'add_op')

        # Condition: Is A > B?
        G.add_edge('val_A', 'cond_op') # input 1 for '>'
        G.add_edge('val_B', 'cond_op') # input 2 for '>'

        # TS inputs: condition, value if true
        G.add_edge('cond_op', 'ts_op') # condition
        G.add_edge('const_true_path', 'ts_op') # value if true

        # Final path: result of add_op (if condition false) or result of ts_op (if true)
        # This would typically need a Merge node. For simplicity, let's make TS output to return.
        # Or let add_op output to return and ts_op also, demonstrating multiple paths to return.
        # A return node should ideally have one input.
        # Let's simplify: (A+B) is returned if A > B is true (using TS)
        G.add_edge('add_op', 'ts_op', port_order=1) # Value if true (re-route for demo)
        G.add_edge('ts_op', 'return_node')
        
        # Add metadata again as it might have been missed for default graph
        for n_id in G.nodes():
            meta = infer_op_metadata(G.nodes[n_id])
            for k, v in meta.items():
                G.nodes[n_id][k] = v
        return G


# Enhanced layout with dot option preferred
def create_enhanced_layout(G, layout_type='dot'):
    if not G.nodes(): # Handle empty graph
        return {}
        
    if layout_type == 'dot':
        try:
            return nx.drawing.nx_pydot.graphviz_layout(G, prog='dot') 
        except Exception as e: # Broader exception for any pydot/graphviz issue
            messagebox.showwarning("Layout Error", f"Graphviz 'dot' layout failed: {e}\n"
                                                  "Ensure Graphviz is installed and in PATH, and 'pydot' or 'pygraphviz' Python package is installed.\n"
                                                  "Falling back to spring layout.")
            return nx.spring_layout(G, k=0.5, iterations=50) # Fallback
    elif layout_type == 'neato':
        try:
            return nx.drawing.nx_pydot.graphviz_layout(G, prog='neato')
        except Exception as e:
            messagebox.showwarning("Layout Error", f"Graphviz 'neato' layout failed: {e}\nFalling back to spring layout.")
            return nx.spring_layout(G, k=0.5, iterations=50)
    elif layout_type == 'spring':
        pos = nx.spring_layout(G, k=0.5/(G.number_of_nodes()**0.5) if G.number_of_nodes() > 0 else 0.5, iterations=100)
    elif layout_type == 'shell':
        pos = nx.shell_layout(G, scale=2.0)
    elif layout_type == 'spectral':
        pos = nx.spectral_layout(G, scale=2.0)
    elif layout_type == 'kamada_kawai':
        pos = nx.kamada_kawai_layout(G, scale=1.0)
    else: # Default to spring if unknown
        pos = nx.spring_layout(G, k=0.5/(G.number_of_nodes()**0.5) if G.number_of_nodes() > 0 else 0.5, iterations=100)
    
    # Standardize scaling for non-Graphviz layouts if needed, but usually they self-scale.
    # The hierarchical layout above produces well-spaced coordinates.
    return pos


class DataflowSimulator:
    def __init__(self, root, G, layout):
        self.root = root
        self.root.title("Token-Based Dataflow Simulation")
        self.root.geometry("1400x1000")
        self.root.configure(bg='#f0f0f0')
        
        self.G = G
        self.layout = layout
        self.current_step = 0
        self.input_widgets = {}
        
        self.input_values = {}
        for node_id in self.G.nodes():
            if self.G.nodes[node_id].get('op') == 'FunctionInput':
                self.input_values[node_id] = self.G.nodes[node_id].get('arg_value', 0)
        
        self.executor = TokenBasedExecutor(self.G.copy()) # Simulate on a copy
        
        self.create_widgets()
        self.reset_simulation() # This will call update_plot

    def create_widgets(self):
        main_frame = tk.Frame(self.root, bg='#f0f0f0')
        main_frame.pack(fill='both', expand=True, padx=10, pady=10)
        
        graph_frame = tk.Frame(main_frame, bg='white', relief='sunken', bd=2)
        graph_frame.pack(fill='both', expand=True, side='top')
        
        self.fig, self.ax = plt.subplots(figsize=(14, 8)) # Adjusted for visibility
        self.fig.patch.set_facecolor('white')
        
        self.canvas = FigureCanvasTkAgg(self.fig, master=graph_frame)
        self.canvas.get_tk_widget().pack(fill='both', expand=True, padx=5, pady=5)
        
        control_frame = tk.Frame(main_frame, bg='#e0e0e0', relief='raised', bd=2, height=200) # Increased height for controls
        control_frame.pack(fill='x', side='bottom', pady=(10, 0))
        control_frame.pack_propagate(False)
        
        control_inner = tk.Frame(control_frame, bg='#e0e0e0')
        control_inner.pack(fill='both', expand=True, padx=10, pady=10) # More padding
        
        input_outer_frame = tk.LabelFrame(control_inner, text="Function Inputs", 
                                  font=("Arial", 11, "bold"), bg='#e0e0e0', fg='#333')
        input_outer_frame.pack(side='left', fill='both', expand=True, padx=(0, 10))
        
        # Scrollable frame for inputs
        input_canvas = tk.Canvas(input_outer_frame, bg='#e0e0e0', highlightthickness=0)
        input_scrollbar = ttk.Scrollbar(input_outer_frame, orient="vertical", command=input_canvas.yview)
        scrollable_input_frame = ttk.Frame(input_canvas, style='Inputs.TFrame') # Use ttk.Frame for better styling options
        
        style = ttk.Style()
        style.configure('Inputs.TFrame', background='#e0e0e0')

        scrollable_input_frame.bind(
            "<Configure>",
            lambda e: input_canvas.configure(scrollregion=input_canvas.bbox("all"))
        )
        
        input_canvas_window = input_canvas.create_window((0, 0), window=scrollable_input_frame, anchor="nw")
        input_canvas.configure(yscrollcommand=input_scrollbar.set)
        
        input_nodes_data = []
        for node_id in sorted(self.G.nodes()): # Sort for consistent order
            node_data = self.G.nodes[node_id]
            if node_data.get('op') == 'FunctionInput':
                input_nodes_data.append((node_id, node_data))
        
        if input_nodes_data:
            for i, (node_id, node_data) in enumerate(input_nodes_data):
                row_frame = tk.Frame(scrollable_input_frame, bg='#e0e0e0')
                row_frame.pack(fill='x', padx=5, pady=(2,3)) # Compact padding
                
                param_name = node_data.get('param_name', f'Node {node_id}').strip('"')
                tk.Label(row_frame, text=f"{param_name}:", font=("Arial", 10), 
                        bg='#e0e0e0', width=15, anchor='e').pack(side='left') # Increased width for names
                
                var = tk.StringVar(value=str(self.input_values.get(node_id, 0)))
                entry = tk.Entry(row_frame, textvariable=var, font=("Arial", 10), width=10) # Wider entry
                entry.pack(side='left', padx=(5, 0))
                
                self.input_widgets[node_id] = var
                # Use a lambda with default argument to capture current node_id
                var.trace_add('write', lambda name, index, mode, nid=node_id: self.on_input_change(nid))
            
            input_canvas.pack(side="left", fill="both", expand=True)
            input_scrollbar.pack(side="right", fill="y")
        else:
            tk.Label(input_outer_frame, text="No function inputs in graph.", 
                    font=("Arial", 10), bg='#e0e0e0').pack(padx=10, pady=20, anchor='center')
        
        control_right = tk.LabelFrame(control_inner, text="Simulation Control", 
                                    font=("Arial", 11, "bold"), bg='#e0e0e0', fg='#333')
        control_right.pack(side='right', fill='y', padx=(10, 0), ipadx=10) # Added ipadx
        
        center_frame = tk.Frame(control_right, bg='#e0e0e0')
        center_frame.pack(expand=True, pady=10) # Added padding
        
        self.step_button = tk.Button(center_frame, text="Next Step", command=self.next_step,
                                   font=("Arial", 12, "bold"), bg='#4CAF50', fg='white',
                                   padx=20, pady=8, relief='raised', bd=3, width=12)
        self.step_button.pack(pady=(5, 8)) # Adjusted padding
        
        self.reset_button = tk.Button(center_frame, text="Reset", command=self.reset_simulation,
                                    font=("Arial", 10), bg='#f44336', fg='white',
                                    padx=15, pady=5, relief='raised', bd=2, width=12)
        self.reset_button.pack(pady=8)
        
        self.status_label = tk.Label(center_frame, text="Step: 0", 
                                   font=("Arial", 11, "bold"), bg='#e0e0e0', height=2) # Ensure height for status
        self.status_label.pack(pady=(8, 5))


    def on_input_change(self, node_id):
        try:
            # Try to parse as int, then float, then keep as string if fails
            val_str = self.input_widgets[node_id].get().strip()
            try:
                value = int(val_str)
            except ValueError:
                try:
                    value = float(val_str)
                except ValueError:
                    value = val_str # Keep as string if not number
        except ValueError: # Should not happen with string fallback
            value = 0 # Default if entry is invalid (e.g. empty string for int/float)
        
        self.input_values[node_id] = value
        # Update the main graph G, which will be copied by executor on reset
        if node_id in self.G.nodes:
             self.G.nodes[node_id]['arg_value'] = value
        self.reset_simulation()

    def reset_simulation(self):
        global memory
        memory.clear()
        self.current_step = 0
        
        # Create a fresh copy of G, potentially updated with new input_values
        current_G_copy = self.G.copy()
        for node_id, value in self.input_values.items():
            if node_id in current_G_copy.nodes and current_G_copy.nodes[node_id].get('op') == 'FunctionInput':
                current_G_copy.nodes[node_id]['arg_value'] = value
        
        self.executor = TokenBasedExecutor(current_G_copy)
        # No need to call executor.reset() here as it's a new instance
        
        self.step_button.config(text="Next Step", state='normal', bg='#4CAF50')
        self.status_label.config(text="Step: 0. Ready.")
        self.update_plot()

    def next_step(self):
        if not self.executor.completed:
            step_info = self.executor.step()
            if step_info:
                self.current_step += 1
                self.update_plot() # update_plot will use executor.execution_sequence
                last_executed_nodes = step_info['nodes']
                results = step_info['results']
                self.status_label.config(text=f"Step: {self.current_step}. Nodes: {last_executed_nodes} -> {results}")
                
                if self.executor.completed:
                    self.step_button.config(text=f"Done! Ret: {self.executor.return_value}", state='disabled', bg='#007ACC')
                    self.status_label.config(text=f"Completed! Return: {self.executor.return_value}")
            else: # No step_info means no node could execute
                self.step_button.config(text="No Progress", state='disabled', bg='#FFA500')
                pending_tokens_exist = any(self.executor.pending_tokens[n] for n in self.executor.pending_tokens if self.executor.pending_tokens[n])
                if pending_tokens_exist and not self.executor.completed:
                     self.status_label.config(text=f"Step: {self.current_step}. Stuck. Pending tokens exist.")
                elif not self.executor.completed:
                     self.status_label.config(text=f"Step: {self.current_step}. Stuck. No executable nodes.")


    def update_plot(self):
        self.ax.clear()
        
        if not self.G.nodes():
            self.ax.text(0.5, 0.5, 'Graph is empty or not loaded.', 
                        ha='center', va='center', transform=self.ax.transAxes, fontsize=16, color='red')
            self.canvas.draw()
            return
        
        # Ensure layout is computed if not already, or if G changed (e.g. on error)
        if not self.layout or set(self.layout.keys()) != set(self.G.nodes()):
            print("Recomputing layout as graph nodes changed or layout was empty.")
            # Assuming args.layout is not directly accessible, use a default or re-evaluate
            # For simplicity, let's try to use the 'dot' layout as a common good one.
            self.layout = create_enhanced_layout(self.G, 'dot') 
            if not self.layout and self.G.nodes(): # If still no layout, try spring
                self.layout = create_enhanced_layout(self.G, 'spring')


        # If layout is still empty after trying, inform user and exit plotting.
        if not self.layout and self.G.nodes():
            self.ax.text(0.5, 0.5, 'Layout computation failed. Cannot draw graph.',
                         ha='center', va='center', transform=self.ax.transAxes, fontsize=14, color='red')
            self.canvas.draw()
            return

        # Node colors and sizes
        executed_nodes_in_seq = [step['nodes'] for step in self.executor.execution_sequence]
        node_colors = []
        node_sizes = []
        
        # Determine active edges from the LAST step
        active_edges = []
        last_executed_source_node = None
        if self.executor.execution_sequence:
            last_step = self.executor.execution_sequence[-1]
            # A token must have been produced and the simulation not yet completed for edge to be "active" for propagation
            any_returns = False
            for node in last_step['nodes']:
                if (self.executor.G.nodes[node].get('op') == 'Return'):
                    any_returns = True
                    break
            if last_step['results'] is not None and not any_returns:
                for node in last_step['nodes']:
                    last_executed_source_node = node # The nodes that just fired and produced output
                    for successor in self.executor.G.successors(last_executed_source_node):
                        active_edges.append((last_executed_source_node, successor))
        
        for n in self.G.nodes(): # Iterate through original graph G for consistent node display
            op_type = self.G.nodes[n].get('op', 'Unknown')
            
            if (self.executor.execution_sequence) and (n in (self.executor.execution_sequence[-1])['nodes']): # Nodes that just executed
                node_colors.append('orange')
                node_sizes.append(800)
            elif n in executed_nodes_in_seq: # Executed in a previous step
                if op_type == 'Return' and self.executor.completed and self.executor.return_value is not None:
                    node_colors.append('gold')
                else:
                    node_colors.append('lightgreen')
                node_sizes.append(800)
            else: # Not yet executed
                if op_type == 'Stream': node_colors.append('salmon')
                elif op_type == 'FunctionInput': node_colors.append('lightblue')
                elif op_type == 'Constant': node_colors.append('lightsteelblue')
                elif op_type == 'Return': node_colors.append('wheat')
                else: node_colors.append('lightgray')
                node_sizes.append(700)

        # Draw edges
        all_edges = list(self.G.edges())
        inactive_edges = [e for e in all_edges if e not in active_edges]

        nx.draw_networkx_edges(self.G, self.layout, ax=self.ax, edgelist=inactive_edges,
                               arrowstyle='->', node_size=node_sizes, arrowsize=10,
                               edge_color='black', width=1.5, alpha=0.8,
                               connectionstyle='arc3,rad=0.1')
        if active_edges:
            nx.draw_networkx_edges(self.G, self.layout, ax=self.ax, edgelist=active_edges,
                                   arrowstyle='->', node_size=node_sizes, arrowsize=10,
                                   edge_color='red', width=1.5, alpha=1.0, # Highlight active edge
                                   connectionstyle='arc3,rad=0.1')
        
        # Draw nodes
        nx.draw_networkx_nodes(self.G, self.layout, node_color=node_colors, node_size=node_sizes,
                               ax=self.ax, edgecolors='black')
        
        # Labels
        labels = {}
        for n in self.G.nodes():
            node_data_g = self.G.nodes[n] # From original graph for base info
            op_type = node_data_g.get('op', 'Unknown')
            param_name = node_data_g.get('param_name', '').strip('"')
            
            # Get current value from executor's node_values (output of the node)
            # For FunctionInput/Constant, this value is set when they "execute"
            current_value_str = ""
            if n in self.executor.node_values:
                 val = self.executor.node_values[n]
                 # Format output value for display
                 if isinstance(val, float): current_value_str = f"{val:.2f}"
                 elif isinstance(val, bool): current_value_str = str(val)
                 else: current_value_str = str(val) # Includes int and string
                 current_value_str = f"\n= {current_value_str}"
            elif op_type == 'FunctionInput': # Show initial value if not executed yet
                 current_value_str = f"\n({node_data_g.get('arg_value',0)})"
            elif op_type == 'Constant': # Show initial value if not executed yet
                 current_value_str = f"\n({node_data_g.get('value',0)})"


            base_label = ""
            if op_type == 'FunctionInput': base_label = param_name if param_name else f'In_{n}'
            elif op_type == 'Constant': base_label = "Const"
            elif op_type == 'Stream': base_label = "STR"
            elif op_type == 'Return': base_label = "ret"
            elif op_type == 'BasicBinaryOp': base_label = node_data_g.get('op_symbol', '?')
            elif op_type == 'TS': base_label = "T S" # True Select
            elif op_type == 'FS': base_label = "F S" # False Select
            elif op_type == 'Load': base_label = "ld"
            elif op_type == 'Store': base_label = "st"
            elif op_type == 'Merge': base_label = "M"
            else: base_label = op_type
            
            labels[n] = f"{base_label}{current_value_str}"
        
        nx.draw_networkx_labels(self.G, self.layout, labels, font_size=8, ax=self.ax, 
                                font_weight='normal', verticalalignment='center') # Adjusted font
        
        # Title and Memory Display
        title_text = "Token Dataflow Simulation"
        if self.current_step == 0:
            title_text = "Ready to Start. Click 'Next Step'."
        elif self.executor.completed:
            title_text = f"Simulation Complete! Return Value: {self.executor.return_value}"
        elif self.executor.execution_sequence:
            last_step = self.executor.execution_sequence[-1]
            title_text = f"Step {self.current_step}: Nodes: '{last_step['nodes']}' -> {last_step['results']}"
        
        self.ax.set_title(title_text, fontsize=13, fontweight='bold', pad=15)
        
        memory_str = ", ".join([f"{k}:{v}" for k,v in sorted(memory.items())]) if memory else "{}"
        self.ax.text(0.01, 0.98, f"Memory: {memory_str}", transform=self.ax.transAxes, 
                    fontsize=9, verticalalignment='top',
                    bbox=dict(boxstyle="round,pad=0.3", facecolor="khaki", alpha=0.7))
        
        self.ax.axis('off')
        self.canvas.draw_idle()


def main():
    parser = argparse.ArgumentParser(description="Token-Based Dataflow Graph Simulator")
    parser.add_argument('--layout', choices=['hierarchical', 'spring', 'shell', 'spectral', 'kamada_kawai', 'dot', 'neato'], 
                        default='dot', help="Layout algorithm for the graph (default: dot)")
    parser.add_argument('--dot', default='dfg.dot', help="Path to the .dot file describing the dataflow graph")
    parser.add_argument('--inputs', type=str, help='JSON string of initial input values for FunctionInput nodes (e.g., \'{"node_id1": 10, "node_id2": "hello"}\')')
    args = parser.parse_args()

    G = read_graph(args.dot)
    
    # Apply input arguments from command line if provided
    if args.inputs:
        try:
            cmd_input_values = json.loads(args.inputs)
            for node_id, value in cmd_input_values.items():
                if node_id in G.nodes and G.nodes[node_id].get('op') == 'FunctionInput':
                    G.nodes[node_id]['arg_value'] = value
                    print(f"CLI Input: Set {node_id} to {value}")
        except json.JSONDecodeError:
            print(f"Error: Invalid JSON format for --inputs argument: {args.inputs}")
        except Exception as e:
            print(f"Error processing --inputs: {e}")

    # Compute layout once initially
    # If G is empty (e.g. read_graph failed and default example also failed), layout will be {}
    layout = {}
    if G.nodes(): 
        layout = create_enhanced_layout(G, args.layout)
    else:
        print("Warning: Graph is empty. The visualizer might not display anything.")


    root = tk.Tk()
    app = DataflowSimulator(root, G, layout)
    root.mainloop()

if __name__ == '__main__':
    main()