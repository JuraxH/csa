./build/ca_builder "$1" >tmp_graph.dot
dot -Tpng tmp_graph.dot -o tmp_graph.png
rm tmp_graph.dot
shotwell tmp_graph.png
rm tmp_graph.png
