import json
import dash
from dash import dcc, html
from dash.dependencies import Output, Input
import dash_table
import sys
import os
import re
import plotly.graph_objs as go
import glob

LOG_FILE = sys.argv[1] if len(sys.argv) > 1 else "log/state_log_2025-05-29_18-52-18_UTC.json"

def parse_latest_json(filename):
    """Return the latest JSON object from a comma-separated log file."""
    try:
        with open(filename, "r") as f:
            content = f.read().strip()
            content = re.sub(r',\s*$', '', content)
            objects = re.split(r'\n},\s*\n{', content)
            objects = [o if o.startswith('{') else '{' + o for o in objects]
            objects = [o if o.endswith('}') else o + '}' for o in objects]
            for obj_str in reversed(objects):
                obj_str = obj_str.strip()
                if obj_str:
                    try:
                        return json.loads(obj_str)
                    except Exception:
                        continue
    except Exception:
        pass
    return None

def make_table_rows_for_logical_node(logical_group_id, logical_node):
    group_id = logical_node.get("groupId", "")
    logical_node_id = logical_node.get("logicalId", "")
    rows = []
    for node in logical_node.get("nodes", []):
        rows.append({
            "Logical Group": logical_group_id,
            "Logical Node": logical_node_id,
            "Physical Group": group_id,
            "Node ID": node["id"],
            "isDR": node["isDR"],
            "Queue Size": node["queue_size"],
            "Workload": node["workload"],
        })
    return rows

def get_latest_plainlog_lines(log_dir="log", n_lines=10):
    log_files = sorted(glob.glob(os.path.join(log_dir, "plain_log_*.log")))
    if not log_files:
        return ["No log file found."]
    latest_log = log_files[-1]
    try:
        with open(latest_log, "r") as f:
            lines = f.readlines()
            return lines[-n_lines:] if len(lines) >= n_lines else lines
    except Exception as e:
        return [f"Error reading log: {e}"]

def get_latest_logical_messages(log_dir="log", n_lines=10):
    log_files = sorted(glob.glob(os.path.join(log_dir, "plain_log_*.log")))
    if not log_files:
        return ["No [LOGICAL] or [CROSS-LOGICAL] log found."]
    latest_log = log_files[-1]
    try:
        with open(latest_log, "r") as f:
            lines = f.readlines()
            filtered = [
                line for line in lines
                if "[LOGICAL]" in line or "[CROSS-LOGICAL]" in line
            ]
            return filtered[-n_lines:] if len(filtered) >= n_lines else filtered
    except Exception as e:
        return [f"Error reading log: {e}"]

def get_latest_transfers_from_log(log_dir="log", max_arrows=5):
    """
    Parse up to max_arrows latest [LOGICAL] Fuzzy transfer or [CROSS-LOGICAL] Escalated fuzzy transfer from the log.
    Returns a list of dicts: {"from": ..., "to": ..., "type": ..., "amount": ..., "prob": ...}
    """
    log_files = sorted(glob.glob(os.path.join(log_dir, "plain_log_*.log")))
    if not log_files:
        return []
    latest_log = log_files[-1]
    pattern1 = re.compile(r"\[LOGICAL\] Fuzzy transfer: ([\d\.]+) from (\S+) to (\S+) \(P=([\d\.]+)\)")
    pattern2 = re.compile(r"\[CROSS-LOGICAL\] Escalated fuzzy transfer: ([\d\.]+) from (\S+) \(LogicalNode: \S+\) to (\S+) \(LogicalNode: \S+\) \(P=([\d\.]+)\)")
    transfers = []
    try:
        with open(latest_log, "r") as f:
            lines = f.readlines()
        for line in reversed(lines):
            m1 = pattern1.search(line)
            if m1:
                transfers.append({
                    "from": m1.group(2),
                    "to": m1.group(3),
                    "type": "LOGICAL",
                    "amount": float(m1.group(1)),
                    "prob": float(m1.group(4))
                })
            m2 = pattern2.search(line)
            if m2:
                transfers.append({
                    "from": m2.group(2),
                    "to": m2.group(3),
                    "type": "CROSS-LOGICAL",
                    "amount": float(m2.group(1)),
                    "prob": float(m2.group(4))
                })
            if len(transfers) >= max_arrows:
                break
    except Exception:
        pass
    return transfers

def make_tree_layout_and_grouped_plot(logical_group_id, logical_nodes, plainlog_lines, logical_lines):
    data = []
    node_workloads = {}
    dr_notes = {}  # Map node_id to DR note if isDR

    for ln in logical_nodes:
        logical_node_id = ln.get("logicalId", "")
        group_id = ln.get("groupId", "")
        node_ids = [node["id"] for node in ln.get("nodes", [])]
        workloads = [node["workload"] for node in ln.get("nodes", [])]
        data.append(go.Bar(
            name=f"{logical_node_id} ({group_id})",
            x=node_ids,
            y=workloads
        ))
        for node in ln.get("nodes", []):
            node_workloads[node["id"]] = node["workload"]
            if node.get("isDR", False):
                dr_notes[node["id"]] = f"DR: {node['id']}"

    # Multiple arrow annotations from log
    transfers = get_latest_transfers_from_log()
    arrow_annotation = []
    for transfer in transfers:
        from_id = transfer["from"]
        to_id = transfer["to"]
        transfer_type = transfer["type"]
        amount = transfer["amount"]
        prob = transfer["prob"]
        annotation_text = f"{amount:.2f} (P={prob:.2f})"
        if from_id in node_workloads and to_id in node_workloads:
            if transfer_type == "CROSS-LOGICAL":
                arrow_annotation.extend([
                    dict(
                        x=to_id,
                        y=node_workloads[to_id],
                        ax=from_id,
                        ay=node_workloads[from_id],
                        xref='x',
                        yref='y',
                        axref='x',
                        ayref='y',
                        showarrow=True,
                        arrowhead=2,
                        arrowsize=3,
                        arrowwidth=4,
                        arrowcolor='blue',
                        opacity=1.0,
                        bgcolor='rgba(255,255,0,0.2)',
                    ),
                    dict(
                        x=to_id,
                        y=node_workloads[to_id] + 5,
                        xref='x',
                        yref='y',
                        text=annotation_text,
                        showarrow=False,
                        font=dict(color='blue', size=14, family="monospace"),
                        bgcolor='rgba(255,255,255,0.7)',
                        bordercolor='blue',
                        borderpad=4,
                    )
                ])
            else:
                arrow_annotation.extend([
                    dict(
                        x=to_id,
                        y=node_workloads[to_id],
                        ax=from_id,
                        ay=node_workloads[from_id],
                        xref='x',
                        yref='y',
                        axref='x',
                        ayref='y',
                        showarrow=True,
                        arrowhead=3,
                        arrowsize=2,
                        arrowwidth=2,
                        arrowcolor='red',
                        opacity=0.8,
                    ),
                    dict(
                        x=to_id,
                        y=node_workloads[to_id] + 5,
                        xref='x',
                        yref='y',
                        text=annotation_text,
                        showarrow=False,
                        font=dict(color='red', size=14, family="monospace"),
                        bgcolor='rgba(255,255,255,0.7)',
                        bordercolor='red',
                        borderpad=4,
                    )
                ])

    # Add DR notes as annotations
    for dr_id, note in dr_notes.items():
        if dr_id in node_workloads:
            arrow_annotation.append(dict(
                x=dr_id,
                y=node_workloads[dr_id] + 10,
                xref='x',
                yref='y',
                text=note,
                showarrow=False,
                font=dict(color='black', size=13, family="monospace"),
                bgcolor='rgba(255,255,0,0.15)',
                bordercolor='black',
                borderpad=3,
            ))

    fig = go.Figure(data=data)
    fig.update_layout(
        barmode='group',
        title=f"Workload per Physical Node grouped by Logical Node in {logical_group_id}",
        yaxis_title="Workload",
        xaxis_title="Physical Node",
        height=400 + 40 * len(logical_nodes),
        legend_title="Logical Node (Physical Group)",
        yaxis=dict(range=[0, 100]),
        annotations=arrow_annotation
    )

    tree = html.Div([
        html.H3(f"Logical Group: {logical_group_id}"),
        dcc.Graph(figure=fig, id="grouped-plot"),
        html.Hr(),
        html.H4("Latest Plain Log"),
        html.Pre("".join(plainlog_lines), style={
            "background": "#f8f9fa",
            "border": "1px solid #bdbdbd",
            "padding": "10px",
            "marginBottom": "20px",
            "borderRadius": "8px",
            "fontFamily": "monospace",
            "fontSize": "14px",
            "color": "#333",
            "marginTop": "0"
        }),
        html.H4("Latest [LOGICAL] and [CROSS-LOGICAL] Messages"),
        html.Pre("".join(logical_lines), style={
            "background": "#e3f2fd",
            "border": "1px solid #64b5f6",
            "padding": "10px",
            "marginBottom": "20px",
            "borderRadius": "8px",
            "fontFamily": "monospace",
            "fontSize": "14px",
            "color": "#222",
            "marginTop": "0"
        }),
        html.Div([
            html.Div([
                html.H4(f"Logical Node: {ln.get('logicalId', '')} (Physical Group: {ln.get('groupId', '')})"),
                dash_table.DataTable(
                    columns=[
                        {"name": "Node ID", "id": "Node ID"},
                        {"name": "isDR", "id": "isDR"},
                        {"name": "Queue Size", "id": "Queue Size"},
                        {"name": "Workload", "id": "Workload"},
                    ],
                    data=[
                        {
                            "Node ID": node["id"],
                            "isDR": node["isDR"],
                            "Queue Size": node["queue_size"],
                            "Workload": node["workload"],
                        }
                        for node in ln.get("nodes", [])
                    ],
                    style_table={'overflowX': 'auto', 'marginBottom': '20px'},
                    style_cell={'textAlign': 'left'},
                ),
            ], style={"border": "1px solid #ccc", "padding": "10px", "marginBottom": "20px", "borderRadius": "8px"})
            for ln in logical_nodes
        ])
    ])
    return tree

def make_table_rows(latest_obj):
    rows = []
    if not latest_obj:
        return rows
    logical_group_id = latest_obj.get("id", "Unknown")
    for logical_node in latest_obj.get("logicalNodes", []):
        rows.extend(make_table_rows_for_logical_node(logical_group_id, logical_node))
    return rows

app = dash.Dash(__name__)
app.layout = html.Div([
    html.H2("Logical Group Structure"),
    dcc.Interval(id='interval', interval=2000, n_intervals=0),
    html.Div(id='tree-structure'),
    html.Hr(),
    html.H4("Flat Table of All Physical Nodes"),
    dash_table.DataTable(
        id='data-table',
        columns=[
            {"name": "Logical Group", "id": "Logical Group"},
            {"name": "Logical Node", "id": "Logical Node"},
            {"name": "Physical Group", "id": "Physical Group"},
            {"name": "Node ID", "id": "Node ID"},
            {"name": "isDR", "id": "isDR"},
            {"name": "Queue Size", "id": "Queue Size"},
            {"name": "Workload", "id": "Workload"},
        ],
        data=[],
        style_table={'overflowX': 'auto'},
        style_cell={'textAlign': 'left'},
    )
])

@app.callback(
    [Output('tree-structure', 'children'),
     Output('data-table', 'data')],
    Input('interval', 'n_intervals')
)
def update_tree_and_table(n):
    plainlog_lines = get_latest_plainlog_lines()
    logical_lines = get_latest_logical_messages()
    latest_obj = parse_latest_json(LOG_FILE)
    if latest_obj:
        logical_group_id = latest_obj.get("id", "Unknown")
        logical_nodes = latest_obj.get("logicalNodes", [])
        tree_layout = make_tree_layout_and_grouped_plot(logical_group_id, logical_nodes, plainlog_lines, logical_lines)
        rows = make_table_rows(latest_obj)
    else:
        tree_layout = html.Div("No data available.")
        rows = []
    return tree_layout, rows

if __name__ == '__main__':
    app.run(debug=False, host='0.0.0.0', port=8050)