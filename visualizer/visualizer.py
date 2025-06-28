import argparse
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import json
import tkinter as tk
from tkinter import messagebox, ttk
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import matplotlib
import sys
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
    elif lbl == 'C':
        meta['op'] = 'Carry'
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
        self.execution_sequence = []  # Record of execution steps (list of step_info dicts)
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
            return 0
        elif op_type == 'Load':
            return 3
        elif op_type in ['BasicBinaryOp', 'TS', 'FS', 'Carry']:
            return 2
        elif op_type in ['Merge', 'Store']:
            return 3
        else: 
            return len(list(self.G.predecessors(node_id)))

    def add_token(self, node, token):
        if node in self.pending_tokens:
            self.pending_tokens[node].append(token)
    
    def can_execute(self, node):
        op_type = self.G.nodes[node].get('op', 'Unknown')
        
        if op_type in ['Constant', 'FunctionInput', 'Stream']:
            return True 

        required_inputs = self.get_op_arity(node)
        available_tokens = len(self.pending_tokens[node])
        
        return available_tokens >= required_inputs
    
    def execute_node(self, node):
        op_type = self.G.nodes[node].get('op', 'Unknown')
        op_symbol_for_log = self.G.nodes[node].get('op_symbol', op_type)

        current_input_tokens = self.pending_tokens[node] 
        result_token = None
        consumed_count = 0
        consumed_input_values = []

        arity = self.get_op_arity(node)
        
        if arity > 0 and len(current_input_tokens) >= arity:
            consumed_input_values = [t.value for t in current_input_tokens[:arity]]

        if op_type == 'Constant':
            value = self.G.nodes[node].get('value', 0)
            result_token = Token(value, node)
        
        elif op_type == 'FunctionInput':
            value = self.G.nodes[node].get('arg_value', 0)
            result_token = Token(value, node)
        
        elif op_type == 'Carry':
            if arity == 2 and len(consumed_input_values) >= 2:
                A, B = consumed_input_values[0], consumed_input_values[1]
                if (consumed_input_values == 2):
                    consumed_count = 2
                    result_token = Token(A, node)
                elif (consumed_input_values == 3):
                    consumed_count = 3
                    condition =  consumed_input_values[2] 
                    if (condition):
                        result_token = Token(B, node)

        elif op_type == 'BasicBinaryOp':
            if arity == 2 and len(consumed_input_values) == 2:
                a_val, b_val = consumed_input_values[0], consumed_input_values[1]
                consumed_count = 2
                op_symbol = self.G.nodes[node].get('op_symbol', '+')
                op_symbol_for_log = op_symbol
                
                if isinstance(a_val, bool): a_val = int(a_val)
                if isinstance(b_val, bool): b_val = int(b_val)

                if op_symbol == '+':
                    if isinstance(a_val, (int, float)) and isinstance(b_val, (int, float)):
                        result = a_val + b_val
                    else:
                        result = str(a_val) + str(b_val)
                elif op_symbol == '-':
                    try: result = a_val - b_val
                    except: result = str(a_val) + "-" + str(b_val) # Placeholder for non-numeric
                elif op_symbol == '<<':
                    try: result = a_val << b_val
                    except: result = str(a_val) + "<<" + str(b_val)
                elif op_symbol == '>>':
                    try: result = a_val >> b_val
                    except: result = str(a_val) + ">>" + str(b_val)
                elif op_symbol == '>':
                    try: result = a_val > b_val
                    except TypeError: result = str(a_val) > str(b_val)
                elif op_symbol == '<':
                    try: result = a_val < b_val
                    except TypeError: result = str(a_val) < str(b_val)
                elif op_symbol == '==':
                    result = a_val == b_val
                elif op_symbol == '!=':
                    result = not (a_val == b_val)
                else: result = None
                
                if result is not None: result_token = Token(result, node)
        
        elif op_type == 'Load':
            if arity == 3 and len(consumed_input_values) == 3:
                addr, offset_val, valid_bit = consumed_input_values[0], consumed_input_values[1], consumed_input_values[2] 
                if (valid_bit):
                    consumed_count = 3
                    final_address = addr + offset_val if isinstance(addr, (int,float)) and isinstance(offset_val, (int,float)) else addr # Fallback if not numeric
                    value = memory.get(final_address)
                    if value is not None: 
                        result_token = Token(value, node)
        
        elif op_type == 'Store':
            if arity == 3 and len(consumed_input_values) == 3:
                addr, val_to_store, offset = consumed_input_values[1], consumed_input_values[2], consumed_input_values[0]
                consumed_count = 3
                final_address = addr + offset if isinstance(addr, (int,float)) and isinstance(offset, (int,float)) else addr # Fallback
                memory[final_address] = val_to_store
                valid_bit_out = 1
                result_token = Token(valid_bit_out, node)
        
        elif op_type == 'TS': 
            if arity == 2 and len(consumed_input_values) == 2:
                cond, val = consumed_input_values[0], consumed_input_values[1]
                consumed_count = 2
                if cond: result_token = Token(val, node)
        
        elif op_type == 'FS':
            if arity == 2 and len(consumed_input_values) == 2:
                cond, val = consumed_input_values[0], consumed_input_values[1]
                consumed_count = 2
                if not cond: result_token = Token(val, node)
        
        elif op_type == 'Merge':
            if arity == 3 and len(consumed_input_values) == 3:
                decider, true_val, false_val = consumed_input_values[0], consumed_input_values[1], consumed_input_values[2]
                consumed_count = 3
                result = true_val if decider else false_val
                result_token = Token(result, node)

        if result_token:
            self.node_values[node] = result_token.value
            
        if consumed_count > 0: # Ensure only consumed tokens are removed
            self.pending_tokens[node] = self.pending_tokens[node][consumed_count:]
        elif arity > 0 and consumed_count == 0 and op_type not in ['Constant', 'FunctionInput', 'Stream']:
            # This case implies an op had arity, but didn't logically consume inputs
            # (e.g. condition failed in TS/FS before consumption was set, or Load failed)
            # We still need to remove the tokens that were checked for arity
            self.pending_tokens[node] = self.pending_tokens[node][arity:]


        return {
            'node_id': node,
            'result_token': result_token,
            'consumed_inputs': consumed_input_values if consumed_count > 0 else ([t.value for t in current_input_tokens[:arity]] if arity > 0 else []), # Log what was available if not consumed
            'op_label': op_symbol_for_log
        }
    
    def step(self):
        if self.completed:
            return None
        
        executable_nodes = [node for node in self.G.nodes() if self.can_execute(node)]
        
        if not executable_nodes:
            stuck = any(self.pending_tokens[n] for n in self.pending_tokens)
            # No return here, GUI will handle status
            return None 
        
        execution_details_for_step = []
        for node_to_execute in executable_nodes:
            detail = self.execute_node(node_to_execute)
            execution_details_for_step.append(detail)
        
        step_info = {
            'execution_details': execution_details_for_step,
            'completed': self.completed,
        }
        self.execution_sequence.append(step_info)
        
        if not self.completed:
            for detail in execution_details_for_step:
                result_token = detail['result_token']
                if result_token: # Check if a token was actually produced
                    source_node = detail['node_id'] 
                    for successor in self.G.successors(source_node):
                        self.add_token(successor, Token(result_token.value, source_node))
        
        return step_info


# Read and process the .dot file
def read_graph(dot_path):
    try:
        G_raw = nx.drawing.nx_pydot.read_dot(dot_path)
        G = nx.DiGraph()
        if not G_raw.nodes():
            return G

        # Only add nodes with recognized labels
        for n, data in G_raw.nodes(data=True):
            meta = infer_op_metadata(data)
            if meta.get('op') == 'Unknown':
                print(f"Skipping unknown node: {n}")
                continue
            G.add_node(n, **data, **meta)

        # Add edges only if both endpoints exist in the filtered node set
        G.add_edges_from((u, v, d) for u, v, d in G_raw.edges(data=True) if u in G.nodes and v in G.nodes)

        return G
    except Exception as e:
        print(f"Error reading graph: {e}")
        messagebox.showerror("Graph Read Error", f"Could not read or parse .dot file: {dot_path}\n{e}\n")
        sys.exit()

# Enhanced layout with dot option preferred
def create_enhanced_layout(G, layout_type='dot'):
    if not G.nodes(): return {}
    try:
        if layout_type == 'dot': return nx.drawing.nx_pydot.graphviz_layout(G, prog='dot') 
        elif layout_type == 'neato': return nx.drawing.nx_pydot.graphviz_layout(G, prog='neato')
    except Exception as e:
        messagebox.showwarning("Layout Error", f"Graphviz '{layout_type}' layout failed: {e}\nFalling back to spring layout.")
    # Fallback layouts
    if layout_type in ['spring', 'dot', 'neato']: # dot/neato fallback here
        return nx.spring_layout(G, k=0.5/(G.number_of_nodes()**0.5) if G.number_of_nodes() > 0 else 0.5, iterations=100)
    elif layout_type == 'shell': return nx.shell_layout(G, scale=2.0)
    elif layout_type == 'spectral': return nx.spectral_layout(G, scale=2.0)
    elif layout_type == 'kamada_kawai': return nx.kamada_kawai_layout(G, scale=1.0)
    else: return nx.spring_layout(G, k=0.5/(G.number_of_nodes()**0.5) if G.number_of_nodes() > 0 else 0.5, iterations=100)


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
        
        self.executor = TokenBasedExecutor(self.G.copy())
        
        self.create_widgets()
        self.reset_simulation()

    def create_widgets(self):
        main_frame = tk.Frame(self.root, bg='#f0f0f0')
        main_frame.pack(fill='both', expand=True, padx=1, pady=1)
        
        # Control frame (packed first from the bottom to reserve its space)
        # MODIFIED: Reduced height from 200 to 160
        control_frame = tk.Frame(main_frame, bg='#e0e0e0', relief='raised', bd=2, height=160) 
        control_frame.pack(side='bottom', fill='x', pady=(5, 0))
        control_frame.pack_propagate(False) # Prevent child widgets from shrinking it
        
        control_inner = tk.Frame(control_frame, bg='#e0e0e0')
        control_inner.pack(fill='both', expand=True, padx=5, pady=5)
        
        input_outer_frame = tk.LabelFrame(control_inner, text="Function Inputs", 
                                  font=("Arial", 11, "bold"), bg='#e0e0e0', fg='#333')
        input_outer_frame.pack(side='left', fill='both', expand=True, padx=(0, 10))
        
        input_canvas = tk.Canvas(input_outer_frame, bg='#e0e0e0', highlightthickness=0)
        input_scrollbar = ttk.Scrollbar(input_outer_frame, orient="vertical", command=input_canvas.yview)
        scrollable_input_frame = ttk.Frame(input_canvas, style='Inputs.TFrame')
        style = ttk.Style()
        style.configure('Inputs.TFrame', background='#e0e0e0')
        scrollable_input_frame.bind("<Configure>", lambda e: input_canvas.configure(scrollregion=input_canvas.bbox("all")))
        input_canvas.create_window((0, 0), window=scrollable_input_frame, anchor="nw")
        input_canvas.configure(yscrollcommand=input_scrollbar.set)
        
        input_nodes_data = [(nid, nd) for nid, nd in self.G.nodes(data=True) if nd.get('op') == 'FunctionInput']
        if input_nodes_data:
            for i, (node_id, node_data) in enumerate(sorted(input_nodes_data)):
                row_frame = tk.Frame(scrollable_input_frame, bg='#e0e0e0')
                row_frame.pack(fill='x', padx=5, pady=(2,3))
                param_name = node_data.get('param_name', f'Node {node_id}').strip('"')
                tk.Label(row_frame, text=f"{param_name}:", font=("Arial", 10), bg='#e0e0e0', width=15, anchor='e').pack(side='left')
                var = tk.StringVar(value=str(self.input_values.get(node_id, 0)))
                entry = tk.Entry(row_frame, textvariable=var, font=("Arial", 10), width=10)
                entry.pack(side='left', padx=(5, 0))
                self.input_widgets[node_id] = var
                var.trace_add('write', lambda name, index, mode, nid=node_id: self.on_input_change(nid))
            input_canvas.pack(side="left", fill="both", expand=True)
            input_scrollbar.pack(side="right", fill="y")
        else:
            tk.Label(input_outer_frame, text="No function inputs in graph.", font=("Arial", 10), bg='#e0e0e0').pack(padx=10, pady=20, anchor='center')

        control_right = tk.LabelFrame(control_inner, text="Simulation Control", font=("Arial", 11, "bold"), bg='#e0e0e0', fg='#333')
        control_right.pack(side='right', fill='y', padx=(10, 0), ipadx=10)
        center_frame = tk.Frame(control_right, bg='#e0e0e0')
        center_frame.pack(expand=True, pady=10)
        self.step_button = tk.Button(center_frame, text="Next Step", command=self.next_step, font=("Arial", 12, "bold"), bg='#4CAF50', fg='white', padx=20, pady=8, relief='raised', bd=3, width=12)
        self.step_button.pack(pady=(5, 8))
        self.reset_button = tk.Button(center_frame, text="Reset", command=self.reset_simulation, font=("Arial", 10), bg='#f44336', fg='white', padx=15, pady=5, relief='raised', bd=2, width=12)
        self.reset_button.pack(pady=8)
        self.status_label = tk.Label(center_frame, text="Step: 0", font=("Arial", 11, "bold"), bg='#e0e0e0', height=2) # Assuming status_label should be here
        self.status_label.pack(pady=(8, 5))

        # Log display frame (packed next from the bottom, placing it above controls)
        log_display_frame = tk.LabelFrame(main_frame, text="Execution Log", 
                                          font=("Arial", 11, "bold"), bg='#f0f0f0', fg='#333', relief='groove', bd=2)
        log_display_frame.pack(side='bottom', fill='x', pady=(5, 0), ipady=2)

        # MODIFIED: Reduced height from 8 to 5 lines
        self.log_text_area = tk.Text(log_display_frame, height=5, wrap=tk.WORD, state='disabled', 
                                     font=("Courier New", 9), bg="#ffffff", relief="sunken", bd=1,
                                     tabs=("1c",)) 
        log_scrollbar = ttk.Scrollbar(log_display_frame, orient="vertical", command=self.log_text_area.yview)
        self.log_text_area.config(yscrollcommand=log_scrollbar.set)
        
        log_scrollbar.pack(side='right', fill='y', padx=(0,2), pady=2)
        self.log_text_area.pack(side='left', fill='both', expand=True, padx=2, pady=2)


        # Graph frame (packed from the top, takes all remaining space)
        graph_frame = tk.Frame(main_frame, bg='white', relief='sunken', bd=0)
        graph_frame.pack(side='top', fill='both', expand=True)
        
        self.fig, self.ax = plt.subplots(figsize=(16, 10)) 
        self.fig.subplots_adjust(left=0.0, bottom=0.0, right=1.0, top=1.0)

        self.fig.patch.set_facecolor('white')
        self.canvas = FigureCanvasTkAgg(self.fig, master=graph_frame)

        # Add a toolbar for navigation (pan, zoom)
        self.toolbar = NavigationToolbar2Tk(self.canvas, graph_frame) # Master is 'self' (the main Tkinter window)
        self.toolbar.update()
        # Pack the toolbar first, at the top
        self.toolbar.pack(side=tk.TOP, fill=tk.X, padx=0, pady=0) # Pack toolbar within graph_frame first
 
        # Then pack the canvas widget, allowing it to expand
        self.canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=True, padx=0, pady=0) # Canvas expands within graph_frame


    def on_input_change(self, node_id):
        try:
            val_str = self.input_widgets[node_id].get().strip()
            try: value = int(val_str)
            except ValueError:
                try: value = float(val_str)
                except ValueError: value = val_str 
        except ValueError: value = 0 
        
        self.input_values[node_id] = value
        if node_id in self.G.nodes:
             self.G.nodes[node_id]['arg_value'] = value
        self.reset_simulation()

    def reset_simulation(self):
        global memory
        memory.clear()
        self.current_step = 0
        
        current_G_copy = self.G.copy()
        for node_id, value in self.input_values.items():
            if node_id in current_G_copy.nodes and current_G_copy.nodes[node_id].get('op') == 'FunctionInput':
                current_G_copy.nodes[node_id]['arg_value'] = value
        
        self.executor = TokenBasedExecutor(current_G_copy)
        
        self.step_button.config(text="Next Step", state='normal', bg='#4CAF50')
        self.status_label.config(text="Step: 0. Ready.")

        # Clear and update logger
        self.log_text_area.config(state='normal')
        self.log_text_area.delete('1.0', tk.END)
        self.log_text_area.insert(tk.END, "Simulation reset. Ready to start.\n")
        self.log_text_area.config(state='disabled')
        
        self.update_plot()

    def next_step(self):
        if self.executor.completed:
            return

        step_info = self.executor.step()
        log_entry_header_written = False

        if step_info and step_info.get('execution_details'):
            self.current_step += 1
            
            self.log_text_area.config(state='normal')
            log_header = f"--- Step {self.current_step} ---\n"
            self.log_text_area.insert(tk.END, log_header)
            log_entry_header_written = True

            executed_node_ids_this_step = []
            result_values_this_step = []

            for detail in step_info['execution_details']:
                node_id = detail['node_id']
                op_label = detail['op_label']
                inputs_str = ",".join(map(str, detail['consumed_inputs'])) if detail['consumed_inputs'] else "N/A"
                
                output_val_str = "N/A (no output)"
                if detail['result_token']:
                    val = detail['result_token'].value
                    if isinstance(val, float): output_val_str = f"{val:.2f}"
                    else: output_val_str = str(val)
                
                log_line = f"Node{node_id}:\t{op_label},\tIn:[{inputs_str}],\tOut:{output_val_str}\n"
                self.log_text_area.insert(tk.END, log_line)

                executed_node_ids_this_step.append(node_id)
                if detail['result_token']:
                    result_values_this_step.append(detail['result_token'].value)
            
            self.status_label.config(text=f"Step: {self.current_step}. Nodes: {executed_node_ids_this_step} -> {result_values_this_step}")
            
            if self.executor.completed:
                ret_val_str = f"{self.executor.return_value:.2f}" if isinstance(self.executor.return_value, float) else str(self.executor.return_value)
                self.step_button.config(text=f"Done! Ret: {ret_val_str}", state='disabled', bg='#007ACC')
                self.status_label.config(text=f"Completed! Return: {ret_val_str}")
                self.log_text_area.insert(tk.END, f"--- Simulation Completed. Return Value: {ret_val_str} ---\n")
            
            self.log_text_area.see(tk.END)
            self.log_text_area.config(state='disabled')
            self.update_plot()

        else: # No step_info or no execution_details means no node could execute
            self.step_button.config(text="No Progress", state='disabled', bg='#FFA500')
            pending_tokens_exist = any(self.executor.pending_tokens[n] for n in self.executor.pending_tokens if self.executor.pending_tokens[n])
            
            self.log_text_area.config(state='normal')
            if not log_entry_header_written: # In case current_step wasn't incremented
                 self.log_text_area.insert(tk.END, f"--- Step {self.current_step + 1} (Attempt) ---\n")

            stuck_msg_status = f"Step: {self.current_step}. Stuck. "
            log_msg_details = "No node could execute.\n"
            if pending_tokens_exist and not self.executor.completed:
                stuck_msg_status += "Pending tokens exist."
                log_msg_details += "Graph may be stuck. Pending tokens exist.\n"
            elif not self.executor.completed:
                stuck_msg_status += "No executable nodes."
                log_msg_details += "Graph may be stuck. No executable nodes and not completed.\n"
            
            self.status_label.config(text=stuck_msg_status)
            self.log_text_area.insert(tk.END, log_msg_details) # Changed from log_msg to log_msg_details for clarity
            self.log_text_area.see(tk.END)
            self.log_text_area.config(state='disabled')


    def update_plot(self):
        self.ax.clear()
        if not self.G.nodes():
            self.ax.text(0.5, 0.5, 'Graph is empty.', ha='center', va='center', transform=self.ax.transAxes)
            self.canvas.draw()
            return

        if not self.layout or set(self.layout.keys()) != set(self.G.nodes()):
            self.layout = create_enhanced_layout(self.G, 'dot') 
            if not self.layout and self.G.nodes(): self.layout = create_enhanced_layout(self.G, 'spring')
        if not self.layout and self.G.nodes():
            self.ax.text(0.5, 0.5, 'Layout failed.', ha='center', va='center', transform=self.ax.transAxes)
            self.canvas.draw(); return

        last_step_executed_node_ids = []
        if self.executor.execution_sequence:
            last_step_details = self.executor.execution_sequence[-1]['execution_details']
            last_step_executed_node_ids = [d['node_id'] for d in last_step_details]

        all_executed_node_ids_ever = set()
        for step_log in self.executor.execution_sequence:
            for detail in step_log['execution_details']:
                all_executed_node_ids_ever.add(detail['node_id'])
        
        node_colors, node_sizes = [], []
        active_edges = []

        if self.executor.execution_sequence: 
            last_step_details = self.executor.execution_sequence[-1]['execution_details']

            if not self.executor.completed:
                for detail in last_step_details:
                    if detail['result_token']: 
                        source_node = detail['node_id']
                        for successor in self.executor.G.successors(source_node):
                            active_edges.append((source_node, successor))
        
        for n in self.G.nodes():
            op_type = self.G.nodes[n].get('op', 'Unknown')
            if n in last_step_executed_node_ids:
                node_colors.append('orange'); node_sizes.append(800)
            elif n in all_executed_node_ids_ever:
                node_colors.append('lightgreen' if not (self.executor.completed) else 'gold')
                node_sizes.append(1200)
            else:
                if op_type == 'Stream': node_colors.append('salmon')
                elif op_type == 'FunctionInput': node_colors.append('lightblue')
                elif op_type == 'Constant': node_colors.append('lightsteelblue')
                elif op_type == 'Return': node_colors.append('wheat')
                else: node_colors.append('lightgray')
                node_sizes.append(1100)

        all_edges = list(self.G.edges())
        inactive_edges = [e for e in all_edges if e not in active_edges]
        nx.draw_networkx_edges(self.G, self.layout, ax=self.ax, edgelist=inactive_edges, arrowstyle='->', node_size=node_sizes, arrowsize=10, edge_color='black', width=1.5, alpha=0.8, connectionstyle='arc3,rad=0.1')
        if active_edges:
            nx.draw_networkx_edges(self.G, self.layout, ax=self.ax, edgelist=active_edges, arrowstyle='->', node_size=node_sizes, arrowsize=10, edge_color='red', width=1.5, alpha=1.0, connectionstyle='arc3,rad=0.1')
        
        nx.draw_networkx_nodes(self.G, self.layout, node_color=node_colors, node_size=node_sizes, ax=self.ax, edgecolors='black')
        
        labels = {}
        for n in self.G.nodes():
            node_data_g = self.G.nodes[n]
            op_type = node_data_g.get('op', 'Unknown')
            param_name = node_data_g.get('param_name', '').strip('"')
            current_value_str = ""
            if n in self.executor.node_values:
                 val = self.executor.node_values[n]
                 if isinstance(val, float): current_value_str = f"{val:.2f}"
                 elif isinstance(val, bool): current_value_str = str(val)
                 else: current_value_str = str(val)
                 current_value_str = f"\n= {current_value_str}"
            elif op_type == 'FunctionInput': current_value_str = f"\n({node_data_g.get('arg_value',0)})"
            elif op_type == 'Constant': current_value_str = f"\n({node_data_g.get('value',0)})"

            base_label = ""
            if op_type == 'FunctionInput': base_label = param_name if param_name else f'In_{n}'
            elif op_type == 'Constant': base_label = "Const"
            elif op_type == 'Stream': base_label = "STR"
            elif op_type == 'Return': base_label = "ret"
            elif op_type == 'BasicBinaryOp': base_label = node_data_g.get('op_symbol', '?')
            elif op_type == 'TS': base_label = "TS" 
            elif op_type == 'FS': base_label = "FS" 
            elif op_type == 'Load': base_label = "ld"
            elif op_type == 'Store': base_label = "st"
            elif op_type == 'Merge': base_label = "M"
            elif op_type == 'Carry': base_label = "C"

            else: base_label = op_type
            labels[n] = f"{base_label}{current_value_str}"
        
        nx.draw_networkx_labels(self.G, self.layout, labels, font_size=8, ax=self.ax, font_weight='normal', verticalalignment='center')
        
        memory_str = ", ".join([f"{k}:{v}" for k,v in sorted(memory.items())]) if memory else "{}"
        self.ax.text(0.01, 0.98, f"Memory: {memory_str}", transform=self.ax.transAxes, fontsize=9, verticalalignment='top', bbox=dict(boxstyle="round,pad=0.3", facecolor="khaki", alpha=0.7))
        
        self.ax.axis('off')
        self.canvas.draw_idle()

def main():
    parser = argparse.ArgumentParser(description="Token-Based Dataflow Graph Simulator")
    parser.add_argument('--layout', choices=['hierarchical', 'spring', 'shell', 'spectral', 'kamada_kawai', 'dot', 'neato'], default='dot', help="Layout algorithm for the graph")
    parser.add_argument('--dot', default='dfg.dot', help="Path to the .dot file")
    parser.add_argument('--inputs', type=str, help='JSON string of initial input values (e.g., \'{"node_id1": 10}\')') # Added from previous context
    args = parser.parse_args()

    G = read_graph(args.dot)
    if args.inputs: # Added from previous context
        try:
            cmd_input_values = json.loads(args.inputs)
            for node_id, value in cmd_input_values.items():
                if node_id in G.nodes and G.nodes[node_id].get('op') == 'FunctionInput':
                    G.nodes[node_id]['arg_value'] = value
        except json.JSONDecodeError: print(f"Error: Invalid JSON for --inputs: {args.inputs}")
        except Exception as e: print(f"Error processing --inputs: {e}")


    layout = {}
    if G.nodes(): layout = create_enhanced_layout(G, args.layout)
    else: print("Warning: Graph is empty.")

    def on_close():
        root.destroy()
        plt.close('all')  # Close all matplotlib figures
        sys.exit(0)       # Ensure the script terminates

    root = tk.Tk()
    root.protocol("WM_DELETE_WINDOW", on_close)  # Handle the X button
    app = DataflowSimulator(root, G, layout)
    root.mainloop()

if __name__ == '__main__':
    main()