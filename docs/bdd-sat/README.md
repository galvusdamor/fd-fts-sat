# bdd-sat: documentation of the BDD-based SAT encodings

`bdd-sat.tex` describes the `bdd_sat` encoder, its options, the label order finders and the
search engines / length strategies that use it. It uses the JAIR author kit (ACM-based
`jair.cls`, 2025 style; `jair.cls`, `acmart.cls` and the biblatex files are copied from the
kit at https://www.jair.org/index.php/jair/formatting).

Build (needs biber for the bibliography):

    pdflatex bdd-sat && biber bdd-sat && pdflatex bdd-sat && pdflatex bdd-sat

The option tables in the appendix (`options-*.tex`) are generated from the C++ sources and
must not be edited by hand. Regenerate them after changing options:

    python3 extract_options.py
