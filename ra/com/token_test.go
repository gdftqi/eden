package com

import (
	"bytes"
	"encoding/base64"
	"fmt"
	"testing"
)

func TestMarshalRoundTrip(t *testing.T) {
	tk := &Token{Expire: 1234567890, Conv: 2000, UserID: 90000, IP: 0x7f000001}
	for i := range tk.CliPK {
		tk.CliPK[i] = byte(i)
	}
	b := tk.Marshal()
	if len(b) != TokenLen {
		t.Fatalf("len=%d want %d", len(b), TokenLen)
	}
	got, err := Unmarshal(b)
	if err != nil || got.Expire != tk.Expire || got.Conv != tk.Conv ||
		got.UserID != tk.UserID || got.IP != tk.IP || !bytes.Equal(got.CliPK[:], tk.CliPK[:]) {
		t.Fatalf("round-trip mismatch: %+v err=%v", got, err)
	}
}

func TestSealCrossCheck(t *testing.T) {
	edSk, _ := base64.StdEncoding.DecodeString("49snRJko0ayMemUHsZ5c7qj6X0Iq09np7NQBu6njl7w6O/FaWuWLST4QN43BYMwxPJdale2LNDKJ+ry2f5sFyQ==")
	gwPk, _ := base64.StdEncoding.DecodeString("33GrMPexOmZIe+q8+yUN3p+k2FkjHezAMWnpmWswu0w=")
	cliPk, _ := base64.StdEncoding.DecodeString("IeXygWC1oAuSDeZp76WiWTkAj/VvWqs+NJ043/bG2Bo=")
	tk := &Token{Expire: 2097693966, Conv: 2000, UserID: 90000, IP: 0}
	copy(tk.CliPK[:], cliPk)
	sealed, err := tk.Seal(edSk, gwPk)
	if err != nil || len(sealed) != TokenSealedLen {
		t.Fatalf("seal failed: len=%d err=%v", len(sealed), err)
	}
	fmt.Println("SEALED_B64=" + base64.StdEncoding.EncodeToString(sealed))
}
