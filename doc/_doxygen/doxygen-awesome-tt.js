/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

window.addEventListener("DOMContentLoaded", () => {
    const versionNode = document.getElementById("projectnumber");
    const titleTable = document.querySelector("#titlearea table");

    if (versionNode && titleTable) {
        const version = versionNode.innerText;
        const cell = titleTable.insertRow(1).insertCell(0);
        cell.innerHTML = `<div id="projectversion">${version}</div>`;
    }

    const topEl = document.getElementById("top");
    if (topEl) {
        const banner = document.createElement("div");
        banner.id = "tt-back-to-docs";
        banner.innerHTML =
            '\u2190 <a href="/tt-system-firmware/">Back to TT-System-Firmware Documentation</a>';
        topEl.parentNode.insertBefore(banner, topEl);
    }

    function replaceArrows(root) {
        const arrows = (root || document).querySelectorAll("#nav-tree .arrow");
        arrows.forEach((el) => {
            const text = el.textContent.trim();
            if (text === "\u25BC" || text === "\u25BE") {
                el.textContent = "[\u2212]";
            } else if (text === "\u25B6" || text === "\u25B8") {
                el.textContent = "[+]";
            }
        });
    }

    replaceArrows();

    const navTree = document.getElementById("nav-tree-contents");
    if (navTree) {
        new MutationObserver(() => replaceArrows()).observe(navTree, {
            childList: true,
            subtree: true,
            characterData: true,
        });
    }

    const navPath = document.getElementById("nav-path");
    if (navPath) {
        const ul = navPath.querySelector("ul");
        if (ul) {
            const navelems = ul.querySelectorAll("li.navelem");
            const footerLi = ul.querySelector("li.footer");

            const crumbLine = document.createElement("div");
            crumbLine.className = "tt-breadcrumbs";
            navelems.forEach((li, i) => {
                if (i > 0) {
                    const sep = document.createElement("span");
                    sep.className = "tt-crumb-sep";
                    sep.textContent = "/";
                    crumbLine.appendChild(sep);
                }
                const link = li.querySelector("a") || li.querySelector("b");
                if (link) crumbLine.appendChild(link.cloneNode(true));
            });

            const footerDiv = document.createElement("div");
            footerDiv.className = "tt-footer";
            if (footerLi) footerDiv.innerHTML = footerLi.innerHTML;

            ul.classList.add("tt-hidden");
            navPath.appendChild(crumbLine);
            navPath.appendChild(footerDiv);
        }
    }
});
