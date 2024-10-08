#include <Arduino.h>
#include <micro_ros_arduino.h>

#include <stdio.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <std_msgs/msg/float32.h>
#include <std_msgs/msg/float32_multi_array.h>

// node
rcl_node_t node;
rclc_support_t support;
rcl_allocator_t allocator;

// publisher
rcl_publisher_t mt_pub1;
std_msgs__msg__Float32 pub_msg1;
rclc_executor_t executor_pub;


// subscription
rcl_subscription_t mt_sub1;
//std_msgs__msg__Float32 sub_msg;
std_msgs__msg__Float32MultiArray sub_msg;
#define ARRAY_LEN 2
rclc_executor_t executor_sub;

#define LED_PIN 13

#define RCCHECK(fn)              \
  {                              \
    rcl_ret_t temp_rc = fn;      \
    if ((temp_rc != RCL_RET_OK)) \
    {                            \
      error_loop();              \
    }                            \
  }
#define RCSOFTCHECK(fn)          \
  {                              \
    rcl_ret_t temp_rc = fn;      \
    if ((temp_rc != RCL_RET_OK)) \
    {                            \
    }                            \
  }

void error_loop()
{
  while (1)
  {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(100);
  }
}

#define ESP
#include <SPI.h>
#include <mcp2515.h>
#include "CalPID.h"
#include "C620.h"

// #########################
struct can_frame readMsg;
struct can_frame sendMsg;
MCP2515 mcp2515(5);

void calOut();
hw_timer_t *hw_timer = NULL;
void IRAM_ATTR onTimer()
{
  calOut();
}
CalPID pid1(0.0025, 0.0018, 0, 0.05, 10);
C620 driver1(&mcp2515, &pid1, &sendMsg, 1);
C620 driver2(&mcp2515, &pid1, &sendMsg, 2);
float target_rad1;
float target_rad2;
int pid_cnt = 0;
int output_cnt = 0;
float rad1;
// #########################
void sub1_cb(const void *msgin){
  const std_msgs__msg__Float32MultiArray * sub_msg = (const std_msgs__msg__Float32MultiArray *)msgin;
  target_rad1 = sub_msg->data.data[0];
  driver1.updatePID_rad(target_rad1);
  pub_msg1.data = target_rad1;
  rcl_publish(&mt_pub1, &pub_msg1, NULL);
}

// void sub1_cb(const void *msgin){
//   const std_msgs__msg__Float32 * sub_msg = (const std_msgs__msg__Float32 *)msgin;
//   target_rad1 = sub_msg->data;
//   driver1.updatePID_rad(target_rad1);
//   pub_msg1.data = target_rad1;
//   rcl_publish(&mt_pub1, &pub_msg1, NULL);
// }
void calOut()
{
  long start = micros();
  if (mcp2515.readMessage(&readMsg) == MCP2515::ERROR_OK)
  {

    if (readMsg.can_id == 0x201)
    {
      driver1.setCANData(&readMsg);
    }
    driver1.update();
  }
  pid_cnt++;
  if (pid_cnt >= 10)
  {
    driver1.updatePID_rad(target_rad1);
    mcp2515.sendMessage(&sendMsg);
    pid_cnt = 0;
  }
  output_cnt++;
  if (output_cnt >= 40)
  {
    rad1 = driver1.readRad_s();
    output_cnt = 0;
  }
}


void setup()
{
  Serial.begin(115200);
  set_microros_transports();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  delay(2000);

  allocator = rcl_get_default_allocator();

  // create init_options
  RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));

  // create node
  RCCHECK(rclc_node_init_default(&node, "micro_ros_arduino_node", "", &support));

  // create publisher
  RCCHECK(rclc_publisher_init_default(
      &mt_pub1,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32),
      "/C620/actual_rad1"));

  // create subscription
  RCCHECK(rclc_subscription_init_default(
      &mt_sub1,
      &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32MultiArray),
      "/C620/target_rad1"));

 

  // create executor
  RCCHECK(rclc_executor_init(&executor_sub, &support.context, 1, &allocator));
  RCCHECK(rclc_executor_add_subscription(&executor_sub, &mt_sub1, &sub_msg, &sub1_cb, ON_NEW_DATA));

  sub_msg.data.data = (float *)malloc(sizeof(float) * ARRAY_LEN);
  sub_msg.data.size = 0;
  sub_msg.data.capacity = ARRAY_LEN;

  pub_msg1.data = 0.0;




  sendMsg.can_id = 0x200;
  sendMsg.can_dlc = 8;
  for (int i = 0; i < 8; i++)
  {
    sendMsg.data[i] = 0;
  }
  mcp2515.reset();
  mcp2515.setBitrate(CAN_1000KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  delay(1000);

  // start timer
  hw_timer = timerBegin(0, 80, true);
  timerAttachInterrupt(hw_timer, &onTimer, true);
  timerAlarmWrite(hw_timer, 5000, true);
  timerAlarmEnable(hw_timer);

  // Serial.println("start");
}

void loop()
{
  delay(100);
  RCSOFTCHECK(rclc_executor_spin_some(&executor_pub, RCL_MS_TO_NS(100)));
  RCSOFTCHECK(rclc_executor_spin_some(&executor_sub, RCL_MS_TO_NS(100)));
  

}
