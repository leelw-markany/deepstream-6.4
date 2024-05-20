# Kafka
https://maouriyan.medium.com/how-to-stream-messages-on-deepstream-using-kafka-d7e39de53003
https://kafka.apache.org/downloads
https://kafka.apache.org/quickstart

zookeeper-server-start.sh -daemon config/zookeeper.properties
kafka-server-start.sh -daemon config/server.properties

kafka-topics.sh --list --bootstrap-server localhost:9092
kafka-topics.sh --create --topic prototype-events --bootstrap-server localhost:9092
kafka-topics.sh --describe --topic prototype-events --bootstrap-server localhost:9092

kafka-console-producer.sh --topic prototype-events --bootstrap-server localhost:9092
kafka-console-consumer.sh --topic prototype-events --from-beginning --bootstrap-server localhost:9092

# 기본 설정값 관련
cat /opt/apache/kafka_2.13-3.6.1/config/server.properties
log.retention.hours:
    로그 세그먼트가 보존되는 최소 시간을 시간 단위로 지정합니다.
    예를 들어, log.retention.hours=168는 로그 세그먼트가 생성된 후 7일 동안 유지됨을 의미합니다.
log.retention.bytes:
    로그 세그먼트의 크기를 제한하여 일정한 디스크 공간을 유지하도록 합니다.
    설정된 바이트 수에 도달하면 가장 오래된 로그 세그먼트가 삭제됩니다.
log.segment.bytes:
    이 설정은 각 로그 세그먼트 파일의 최대 크기를 지정합니다.
    설정된 크기에 도달하면 새로운 세그먼트 파일이 생성됩니다.
log.retention.check.interval.ms:
    이 설정은 보존 정책을 검사하여 로그 세그먼트를 삭제하는 빈도를 제어합니다.
    설정된 시간(밀리초 단위)마다 보존 정책이 적용되고 로그 세그먼트가 삭제될 수 있습니다.