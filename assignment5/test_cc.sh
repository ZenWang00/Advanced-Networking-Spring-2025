#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
else
    OS="linux"
fi


generate_test_data() {
    local size=$1
    dd if=/dev/urandom of=test_data.bin bs=1M count=$size
}


cleanup() {
    rm -f test_data.bin receiver.log
    if [ "$OS" = "linux" ]; then
        ./network.sh off
    fi
    
    pkill -f "./receiver" 2>/dev/null
    pkill -f "./sender" 2>/dev/null
}


setup_network() {
    if [ "$OS" = "linux" ]; then
        ./network.sh on
        ./network.sh link delay 100ms 20ms loss 5%
    else
        echo "Running on macOS - using local network"
        :
    fi
}

run_test() {
    local data_size=$1
    local test_name=$2
    
    echo "Running test: $test_name"
    echo "Data size: $data_size MB"
    
    rm -f test_data.bin receiver.log
    
    generate_test_data $data_size
    
    if [ "$OS" = "linux" ]; then

        ip netns exec H2 ./receiver > receiver.log 2>&1 &
        receiver_pid=$!
        sleep 2
        ip netns exec H1 ./sender < test_data.bin
    else
        echo "Please run the following command in a new terminal:"
        echo "./receiver sa=127.0.0.1 ra=127.0.0.1 sp=3456 rp=6543 -v > receiver.log 2>&1"
        echo "Press Enter when ready..."
        read
        
        ./sender sa=127.0.0.1 ra=127.0.0.1 sp=3456 rp=6543 -v < test_data.bin > sender.log 2>&1 &
        sender_pid=$!
        
        for i in {1..300}; do
            if ! kill -0 $sender_pid 2>/dev/null; then
                break
            fi
            if [ $((i % 10)) -eq 0 ]; then
                echo "Test in progress... $i seconds elapsed"
            fi
            sleep 1
        done
        
        if kill -0 $sender_pid 2>/dev/null; then
            echo "Warning: Test timed out after 5 minutes"
            kill $sender_pid 2>/dev/null
            pkill -f "./receiver" 2>/dev/null
            return 1
        fi
    fi
    
    if [ "$OS" = "linux" ]; then
        wait $receiver_pid
    fi
    
    echo "Test completed. Analyzing results..."
    if [ -f receiver.log ]; then
        echo "Receiver log summary:"
        tail -n 20 receiver.log
    else
        echo "Warning: No receiver log found"
    fi
    
    pkill -f "./receiver" 2>/dev/null
    pkill -f "./sender" 2>/dev/null
    
    sleep 2
}

main() {
    setup_network

    for size in 1 5 10; do
        if ! run_test $size "Test with ${size}MB data"; then
            echo "Test with ${size}MB data failed, stopping..."
            break
        fi
        echo "Waiting 5 seconds before next test..."
        sleep 5
    done

    cleanup
}

main 