#!/usr/bin/env python3
#
# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import argparse
import glob
import os

import pandas as pd
import plotly.graph_objects as go

# Parse command-line arguments
parser = argparse.ArgumentParser(
    description="Render CSVs as a Plotly graph.", allow_abbrev=False
)
parser.add_argument("csv_dir", type=str, help="Directory containing CSV files")
parser.add_argument("html_file", type=str, help="Output HTML file")
parser.add_argument("board", type=str, help="Board name")
args = parser.parse_args()

csv_files = glob.glob(os.path.join(args.csv_dir, "*.csv"))

fig = go.Figure()

for csv_path in csv_files:
    df = pd.read_csv(csv_path)
    label = os.path.splitext(os.path.basename(csv_path))[0]
    x = df["Timestamp"] if df["Timestamp"].notnull().any() else df.index
    y = list(
        map(
            lambda v: float(v.strip("%")) if isinstance(v, str) else v,
            df["Failure Percentage"],
        )
    )
    hover_text = [
        (
            f"Fail Count: {fc}<br>Total Tries: {tt}<br> Failure Percentage: {pr}<br>Commit: {cm}<br>Branch: {br}<br>"
            f"Workflow: <a href='{url}' target='_blank'>{url}</a><br>Timestamp: {ts}"
        )
        for fc, tt, pr, cm, br, url, ts in zip(
            df["Fail Count"],
            df["Total Tries"],
            df["Failure Percentage"],
            df["Commit"],
            df["Branch"],
            df["Workflow Run URL"],
            df["Timestamp"],
        )
    ]
    fig.add_trace(
        go.Scatter(
            x=x,
            y=y,
            mode="lines+markers",
            name=label,
            hoverinfo="text",
            hovertext=hover_text,
            customdata=df["Workflow Run URL"],
            hoverlabel=dict(
                bgcolor="lightyellow", font=dict(color="black"), bordercolor="blue"
            ),
        )
    )

fig.update_layout(
    title=f"Stress Test Results ({args.board.upper()})",
    xaxis_title="Timestamp",
    yaxis_title="Failure Percentage",
    yaxis=dict(range=[0, 10]),
    height=700,
    showlegend=True,
)


# Save to HTML
fig.write_html(args.html_file)

# Inject JavaScript to open link on point click
with open(args.html_file, "r") as f:
    html = f.read()

inject_js = """<script>
document.addEventListener('DOMContentLoaded', function() {
    var plot = document.getElementsByClassName('plotly-graph-div')[0];
    if (plot) {
        plot.on('plotly_click', function(data){
            var url = data.points[0].customdata;
            if (url) {
                window.open(url, '_blank');
            }
        });
    }
});
</script>"""

# Insert before </body>
html = html.replace("</body>", inject_js + "\n</body>")

with open(args.html_file, "w") as f:
    f.write(html)

print(f"HTML file generated: {args.html_file}")
