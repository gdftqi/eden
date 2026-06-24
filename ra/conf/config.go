package conf

import (
	"os"

	"gopkg.in/yaml.v3"
)

type config struct {
	Host        string `yaml:"host"`
	GwEd25519SK string `yaml:"gw_ed25519_sk"`
	GwX25519PK  string `yaml:"gw_x25519_pk"`
}

var Instance *config

func Init(fname string) error {
	data, err := os.ReadFile(fname)
	if err != nil {
		return err
	}

	tmp := config{}
	err = yaml.Unmarshal(data, &tmp)
	if err != nil {
		return err
	}

	Instance = &tmp
	return nil
}
