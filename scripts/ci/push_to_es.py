#!/usr/bin/env python3
#
# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import os
import glob
import csv
from datetime import datetime

from elasticsearch import Elasticsearch

es_url = os.getenv("ES_URL")
es_username = os.getenv("ES_USERNAME")
es_password = os.getenv("ES_PASSWORD")
board = os.getenv("BOARD", "unknown")

if not es_url:
    print("ES_URL not set. Skipping push to Elasticsearch.")
    raise SystemExit(0)

es = Elasticsearch(
    es_url,
    basic_auth=(es_username, es_password) if es_username and es_password else None,
    verify_certs=False,
)

if not es.ping():
    print("Failed to connect to Elasticsearch")
    raise SystemExit(1)

index = f"ci-runs-{datetime.utcnow().strftime('%Y.%m.%d')}"
print(f"Pushing CI stats to {index} (board: {board})")

count = 0
for csv_file in glob.glob("*results.csv"):
    test_name = csv_file.replace(" results.csv", "").replace(" ", "_")
    print(f"Processing: {csv_file} → test_name: {test_name}")
    with open(csv_file) as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row["Branch"] != "main":
                continue
            try:
                doc = {
                    "@timestamp": row["Timestamp"],
                    "board": board,
                    "test_name": test_name,
                    "commit": row["Commit"],
                    "fail_count": int(row["Fail Count"]),
                    "total_tries": int(row["Total Tries"]),
                    "failure_rate": float(row["Failure Percentage"].rstrip("%")),
                    "workflow_url": row["Workflow Run URL"],
                }
                result = es.index(index=index, document=doc)
                print(
                    f"  Indexed: {doc['test_name']}@{doc['commit'][:7]} → {result['result']}"
                )
                count += 1
            except Exception as e:
                print(f"  Failed: {e}")

print(f"Success: {count} documents indexed")
