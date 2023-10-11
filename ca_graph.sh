if [ ! -f "./build/ca_cli" ]; then
    echo "ca_cli must be built first";
    exit 1;
fi

./build/ca_cli debug ca "$1" | dot -Tpng -o tmp_graph.png
if [ ! -f "tmp_graph.png" ]; then
    echo "failed to build the graph";
fi
[ -x $(command -v shotwell) ] && { shotwell tmp_graph.png; rm tmp_graph.png; exit 0; }
[ -x $(command -v feh) ] && { feh tmp_graph.png; rm tmp_graph.png; exit 1; }

rm tmp_graph.png
echo "missing program to display the graph"
