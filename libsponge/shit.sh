for file in *.cc *.hh; do
    if [[ -f $file ]]; then
        echo "Formatting $file"
        clang-format -i $file
    fi
done