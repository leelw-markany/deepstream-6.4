# sudo apt install x11-xserver-utils

# 참고용 소스
https://github.com/NVIDIA-AI-IOT/deepstream_reference_apps/

# Kafka
https://maouriyan.medium.com/how-to-stream-messages-on-deepstream-using-kafka-d7e39de53003
https://kafka.apache.org/downloads
https://kafka.apache.org/quickstart

zookeeper-server-start.sh -daemon config/zookeeper.properties
kafka-server-start.sh -daemon config/server.properties

kafka-topics.sh --list --bootstrap-server localhost:9092
kafka-topics.sh --create --topic quickstart-events --bootstrap-server localhost:9092
kafka-topics.sh --describe --topic quickstart-events --bootstrap-server localhost:9092

kafka-console-producer.sh --topic quickstart-events --bootstrap-server localhost:9092
kafka-console-consumer.sh --topic quickstart-events --from-beginning --bootstrap-server localhost:9092
