package com

import (
	"crypto/rand"
	"encoding/binary"
	"errors"
	"fmt"
	"time"

	"github.com/eva/mid"
)

const __CONV_TTL = 24 * time.Hour

func GenConv(gwID uint32) (uint32, error) {
	var buf [4]byte

	for i := 0; i < 8; i++ {
		if _, err := rand.Read(buf[:]); err != nil {
			return 0, err
		}

		conv := binary.LittleEndian.Uint32(buf[:])
		if conv == 0 {
			continue
		}

		ok, err := mid.RedisSetNx(fmt.Sprintf("conv:%d:%d", gwID, conv), 1, __CONV_TTL)
		if err != nil {
			return 0, err
		}

		if ok {
			return conv, nil
		}
	}

	return 0, errors.New("conv 生成重试耗尽")
}
