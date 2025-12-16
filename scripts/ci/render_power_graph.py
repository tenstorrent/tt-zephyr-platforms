#!/usr/bin/env python3
#
# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0
#
# Script to render aggregated power monitoring CSV data as Plotly graphs

import argparse
import glob
import os

import pandas as pd
import plotly.graph_objects as go

# Parse command-line arguments
parser = argparse.ArgumentParser(
    description="Render power monitoring CSVs as a Plotly graph.", allow_abbrev=False
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

    # Use Timestamp for x-axis
    x = (
        df["Timestamp"]
        if "Timestamp" in df.columns and df["Timestamp"].notnull().any()
        else df.index
    )

    # Check if we have the required columns
    if "Average Power (W)" not in df.columns:
        print(f"Warning: No 'Average Power (W)' column in {csv_path}")
        continue

    # Calculate error bars (asymmetric: upper = max - avg, lower = avg - min)
    avg_power = df["Average Power (W)"].astype(float)
    max_power = (
        df["Max Power (W)"].astype(float)
        if "Max Power (W)" in df.columns
        else avg_power
    )
    min_power = (
        df["Min Power (W)"].astype(float)
        if "Min Power (W)" in df.columns
        else avg_power
    )

    # Error bar values: upper (max - avg) and lower (avg - min)
    error_upper = max_power - avg_power
    error_lower = avg_power - min_power

    # Create hover text with all relevant information
    hover_text = []
    for idx, row in df.iterrows():
        avg_p = row.get("Average Power (W)", "N/A")
        max_p = row.get("Max Power (W)", "N/A")
        min_p = row.get("Min Power (W)", "N/A")
        sample_count = row.get("Sample Count", "N/A")
        commit = row.get("Commit", "N/A")
        branch = row.get("Branch", "N/A")
        workflow_url = row.get("Workflow Run URL", "N/A")
        timestamp = row.get("Timestamp", "N/A")

        hover_text.append(
            f"Average Power: {avg_p}W<br>"
            f"Max Power: {max_p}W<br>"
            f"Min Power: {min_p}W<br>"
            f"Sample Count: {sample_count}<br>"
            f"Commit: {commit}<br>"
            f"Branch: {branch}<br>"
            f"Workflow: <a href='{workflow_url}' target='_blank'>{workflow_url}</a><br>"
            f"Timestamp: {timestamp}"
        )

    # Add Average Power trace with error bars
    fig.add_trace(
        go.Scatter(
            x=x,
            y=avg_power,
            mode="lines+markers",
            name=label,
            hoverinfo="text",
            hovertext=hover_text,
            customdata=(
                df["Workflow Run URL"] if "Workflow Run URL" in df.columns else None
            ),
            error_y=dict(
                type="data",
                symmetric=False,
                array=error_upper,
                arrayminus=error_lower,
                color="rgba(0, 0, 0, 0.3)",
                thickness=1.5,
                width=3,
            ),
            line=dict(color="blue", width=2),
            marker=dict(size=6, color="blue"),
            hoverlabel=dict(
                bgcolor="lightyellow", font=dict(color="black"), bordercolor="blue"
            ),
        )
    )

fig.update_layout(
    title=f"Board Power Monitoring ({args.board.upper()})",
    xaxis_title="Timestamp",
    yaxis_title="Average Power (Watts)",
    height=700,
    showlegend=True,
)

# Save to HTML
fig.write_html(args.html_file)

# Inject JavaScript to open link on point click
with open(args.html_file, "r") as f:
    html = f.read()

inject_js = """
<script>
document.addEventListener('DOMContentLoaded', function(){
    var plot = document.getElementsByClassName('plotly-graph-div')[0];
    if (plot) {
        plot.on('plotly_click', function(data) {
            var url = data.points[0].customdata;
            if (url) {
                window.open(url, '_blank');
            }
        });
    }
});
</script>
"""

# Insert before </body>
html = html.replace("</body>", inject_js + "\n</body>")

with open(args.html_file, "w") as f:
    f.write(html)

print(f"HTML file generated: {args.html_file}")
