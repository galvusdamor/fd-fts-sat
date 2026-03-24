import logging
import os.path

from lab import tools


class NiceScatterPgfplots:
    @classmethod
    def _get_plot(cls, report):
        lines = []
        options = cls._get_axis_options(report)
        if report.x_upper is not None:
            options["xmax"] = report.x_upper
        if report.y_upper is not None:
            options["ymax"] = report.y_upper
        lines.append(f"\\begin{{axis}}[{cls._format_options(options)}]")
        
        sorted_categories = sorted(report.categories, key=report.sort_categories) if hasattr(report,"sort_categories") else sorted(report.categories)
        
        for category in sorted_categories:
            coords = report.categories[category]
            lines.append(
                "\\addplot+[{}] coordinates {{\n{}\n}};".format(
                    cls._format_options({"only marks": True}),
                    " ".join(str(c) for c in coords),
                )
            )

            if category:
                if report.hide_legend:
                    lines.append(f"%\\addlegendentry{{{category}}}")
                else:
                    lines.append(f"\\addlegendentry{{{category}}}")

            elif report.has_multiple_categories():
                # None is treated as the default category if using multiple
                # categories. Add a corresponding entry to the legend.
                lines.append("\\addlegendentry{default}")

        if report.plot_horizontal_line:
            # Add black line at y=1.
            line_min, line_max = cls._get_supported_range(options["xmode"])
            lines.append(
                f"\\draw[color=black] (axis cs:{line_min},1) -- "
                f"(axis cs:{line_max},1);"
            )
        if report.plot_diagonal_line:
            # Add black diagonal line.
            assert options["xmode"] == options["ymode"]
            line_min, line_max = cls._get_supported_range(options["xmode"])
            lines.append(
                f"\\draw[color=black] (axis cs:{line_min},{line_min}) -- "
                f"(axis cs:{line_max},{line_max});"
            )

        extra_diagonal_lines_dash = {
            1 : 'dashed',
            2: 'loosely dashed',
            3: 'loosely dashed',
            4: 'loosely dashed',
        }

        for i in range(1, report.num_extra_diagonal_lines+1):
            # Add intermediate diagonal lines.
            assert options["xmode"] == options["ymode"]
            line_min, line_max = cls._get_supported_range(options["xmode"])
            line_min, line_max = float(line_min), float(line_max)
            lines.append(
                f"\\draw[color=black, , {extra_diagonal_lines_dash[i]}] (axis cs:{line_min},{line_min*pow(10,i)}) -- "
                f"(axis cs:{line_max},{line_max*pow(10,i)});"
            )
            lines.append(
                f"\\draw[color=black, {extra_diagonal_lines_dash[i]}] (axis cs:{line_min*pow(10,i)},{line_min}) -- "
                f"(axis cs:{line_max*pow(10,i)},{line_max});"
            )

        lines.append("\\end{axis}")
        return lines

    @classmethod
    def _get_supported_range(cls, mode):
        # These are approximate values found by trial and error.
        if mode == "normal":
            return "-1e3", "1e3"
        else:
            assert mode == "log"
            return "1e-70", "1e70"

    @classmethod
    def write(cls, report, filename):
        if report.ommit_preamble:
            lines = cls._get_plot(report)
            
        else:            
            lines = (
                [
                    r"\documentclass[tikz]{standalone}",
                    r"\usepackage{pgfplots}",
                ]
                + report.extra_preamble
                + [
                    r"\begin{document}",
                    r"\begin{tikzpicture}",
                ]
                + cls._get_plot(report)
                + [r"\end{tikzpicture}", r"\end{document}"]
            )
        tools.makedirs(os.path.dirname(filename))
        tools.write_file(filename, "\n".join(lines))
        logging.info(f"Wrote file://{filename}")

    @classmethod
    def _get_axis_options(cls, report):
        axis = {}
        if report.axis_options:
            axis = report.axis_options
            
        axis["xlabel"] = report.xlabel
        axis["ylabel"] = report.ylabel
        if report.title: 
            axis["title"] = report.title
        if "legend cell align" not in report.axis_options and not report.hide_legend: 
            axis["legend cell align"] = "left"

            if report.has_multiple_categories():
                axis["legend style"] = cls._format_options(
                    {"legend pos": "outer north east"}
                )

        convert_scale = {"log": "log", "symlog": "log", "linear": "normal"}
        axis["xmode"] = convert_scale[report.xscale]
        axis["ymode"] = convert_scale[report.yscale]

        axis["width"] = report.width
        axis["height"] = report.height


        return axis

    @classmethod
    def _format_options(cls, options):
        opts = []
        for key, value in sorted(options.items()):
            if value is None or value is False:
                continue
            if isinstance(value, bool) or value is None:
                opts.append(key)
            elif isinstance(value, str):
                if " " in value or "=" in value:
                    value = f"{{{value}}}"
                opts.append(f"{key}={value.replace('_', '-')}")
            else:
                opts.append(f"{key}={value}")
        return ", ".join(opts)
