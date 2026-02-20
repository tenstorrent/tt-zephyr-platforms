# Minimal makefile for Sphinx documentation
#

# You can set these variables from the command line, and also
# from the environment for the first two.
SPHINXOPTS    ?= -W --conf-dir .
SPHINXBUILD   ?= sphinx-build
SPHINXWATCH   ?= sphinx-autobuild
SOURCEDIR     = .
BUILDDIR      = _build_sphinx

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)

# Development (watch): build the documentation and watch for changes.
# Served on port 3000 (http://localhost:3000)
watch:
	@$(SPHINXWATCH) "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O) --port 3000 --open-browser

.PHONY: help doxygen Makefile

doxygen:
	doxygen Doxyfile
	@mkdir -p "$(BUILDDIR)/html/doxygen"
	cp -r _build_doxygen/html/* "$(BUILDDIR)/html/doxygen/"

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(SPHINXOPTS).
%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCEDIR)" "$(BUILDDIR)" $(SPHINXOPTS) $(O)
