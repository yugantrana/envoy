for i in {1..50}; do
    (/google/data/ro/teams/quic/tools/quic_client --host=127.0.0.1 --port=8009 -disable_certificate_verification www.facebook.com -quiet --num_requests=10 &)
done