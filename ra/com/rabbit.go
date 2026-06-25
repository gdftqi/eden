package com

import (
	"fmt"

	amqp "github.com/rabbitmq/amqp091-go"
)

var Rabbit *amqp.Connection

type RabbitConfig struct {
	Host        string `yaml:"host"`
	Port        int    `yaml:"port"`
	User        string `yaml:"user"`
	Password    string `yaml:"password"`
	VirtualHost string `yaml:"virtual_host"`
}

func InitRabbit(c *RabbitConfig) error {
	conn, err := amqp.Dial(fmt.Sprintf("amqp://%s:%s@%s:%d/%s", c.User, c.Password, c.Host, c.Port, c.VirtualHost))
	if err != nil {
		return err
	}

	Rabbit = conn
	return nil
}

func CreateChannel() (*amqp.Channel, error) {
	ch, err := Rabbit.Channel()
	if err != nil {
		return nil, err
	}

	return ch, nil
}

func CreateQueue(ch *amqp.Channel, name string, duration ...bool) (*amqp.Queue, error) {
	d := false
	if len(duration) > 0 && duration[0] {
		d = true
	}

	que, err := ch.QueueDeclare(name, d, false, true, false, nil)
	if err != nil {
		return nil, err
	}

	return &que, nil
}

func ConsumeQueue(ch *amqp.Channel, queue *amqp.Queue) (<-chan amqp.Delivery, error) {
	return ch.Consume(queue.Name, "", true, false, false, false, nil)
}
