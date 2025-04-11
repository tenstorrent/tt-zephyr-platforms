#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
This script filters runners listed in .github/runners.yml, using the 'label' field, to see if any
are available GitHub runners supports a specific label.

Usage:
    check-runners.py -t <TOKEN> -o <OWNER_OR_ORG> -r <REPO> -y .github/runners.yml

E.g. with a token file:
    check-runners.py -t ~/.ghtoken -o me -r code -y .github/runners.yml
    {"config": [{"board": "p100", "label": "yyz-zephyr-lab-p100"}]}

E.g. with a token string:
    check-runners.py -t ${{ GITHUB_TOKEN }} -y .github/runners.yml
    p100 yyz-zephyr-lab-p100
"""

import argparse
import json
import logging
import requests
import yaml

from pathlib import Path

logger = logging.getLogger(Path(__file__).name.replace(".py", ""))


def parse_args():
    parser = argparse.ArgumentParser(
        description="Check if any available github runner supports a specific label",
        allow_abbrev=False,
    )
    parser.add_argument(
        "-d",
        "--debug",
        help="Display debug info",
        action="store_true",
    )
    parser.add_argument(
        "-j",
        "--json",
        help="Display output in JSON format",
        action="store_true",
    )
    parser.add_argument(
        "-o",
        "--owner",
        metavar="OWNER",
        help="Repository owner or organization",
        default="tenstorrent",
    )
    parser.add_argument(
        "-r",
        "--repo",
        metavar="REPO",
        help="Repository name",
        default="tt-zephyr-platforms",
    )
    parser.add_argument(
        "-t",
        "--token",
        metavar="TOKEN_OR_FILE",
        help="GitHub token string or path to token file",
        required=True,
    )
    parser.add_argument(
        "-y",
        "--runner-yaml",
        metavar="FILE",
        type=Path,
        help="YAML file mapping short-form board names to runner labels",
        required=True,
    )

    args = parser.parse_args()

    # check arguments

    # pull in github token
    token_path = Path(args.token)
    if token_path.exists():
        with open(token_path, "r") as f:
            setattr(args, "token", f.read().strip())
    else:
        # assume token is passed as a string - e.g. ${{ GITHUB_TOKEN }}
        pass

    return args


def runner_exists_at_url(url: str, headers: dict[str, str], label: str) -> bool:
    logger.debug(f"Checking {url} for label {label}")

    try:
        resp = requests.get(url=url, headers=headers)
        data = resp.json()
    except Exception as e:
        logger.debug(f"Failed to fetch runners from {url}: {e}")
        return False

    if resp.status_code != 200:
        logger.debug(
            f"Failed to fetch runners from {url}: {resp.status_code} {resp.text}"
        )
        return False

    if "runners" not in data:
        logger.debug("No runners found in response")
        return False

    for rnr in data["runners"]:
        for lbl in rnr["labels"]:
            if lbl["name"] == label:
                logger.debug(f"Found runner {rnr} with label {label}")
                return True

    logger.debug(f"No runners found matching label {label}")
    return False


def runner_exists(token: str, owner: str, repo: str, label: str):
    urls = [
        f"https://api.github.com/orgs/{owner}/actions/runners",
        f"https://api.github.com/repos/{owner}/{repo}/actions/runners",
    ]
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
    }

    for url in urls:
        if runner_exists_at_url(url=url, headers=headers, label=label):
            return True
    return False


def main():
    logging.basicConfig(level=logging.INFO)

    args = parse_args()

    if args.debug:
        logger.setLevel(logging.DEBUG)

    with open(args.runner_yaml, "r") as f:
        logger.debug(f"Reading {args.runner_yaml}")
        yml = yaml.safe_load(f)

    for i in range(len(yml["config"]) - 1, -1, -1):
        cfg = yml["config"][i]
        if not runner_exists(args.token, args.owner, args.repo, cfg["label"]):
            logger.debug(
                f"Removing entry {i} {cfg['board']} {cfg['label']} from config"
            )
            yml["config"].pop(i)

    if args.json:
        print(json.dumps(yml))
    else:
        for cfg in yml["config"]:
            print(f"{cfg['board']} {cfg['label']}")


if __name__ == "__main__":
    main()
