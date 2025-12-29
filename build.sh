DIRECTORY="build"

if [ -d "$DIRECTORY" ]; then
    echo "\"$DIRECTORY\" directory already exists, building orbit..."
else
    echo "Making \"$DIRECTORY\" directory..."
    mkdir build
fi

cd build
cmake ..
make
